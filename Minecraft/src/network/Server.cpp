#include "network/Server.h"
#include "core.h"
#include "core/Scene.h"
#include "network/Network.h"
#include "world/ChunkManager.h"
#include "world/Chunk.hpp"
#include "world/BlockMap.h"

#include <enet/enet.h>

namespace Minecraft
{
	namespace Server
	{
		// Internal variables
		static ENetAddress address;
		static ENetHost* server;
		static const int maxClients = 32;
		static std::array<ENetPeer*, maxClients> clients;
		static int numConnectedClients;

		static const char* hostname = nullptr;
		static int port = 0;

		// Internal functions
		static void processEvent(NetworkEvent* event, uint8* data);

		void init(const char* inHostname, int inPort)
		{
			g_logger_assert(strcmp(inHostname, "") != 0 && inPort != 0, "Need to supply hostname and port to server initialization.");
			hostname = inHostname;
			port = inPort;

			// Bind the server to the default localhost
			g_logger_warning("Server only supports localhost right now.");
			address.host = ENET_HOST_ANY;
			address.port = port;
			g_memory_zeroMem(clients.data(), sizeof(ENetPeer*) * maxClients);
			numConnectedClients = 0;

			server = enet_host_create(
				&address, // the address to bind the server host to
				32, // allow up to 32 clients and/or outgoing connections
				2, // allow up to 2 channels to be used, 0 and 1
				0, // assume any amount of incoming bandwidth
				0); // assume any amount of outgoing bandwidth

			if (server == NULL)
			{
				g_logger_assert(false, "An error occurred while trying to create an ENet server host.");
				return;
			}
		}

		void update(float dt)
		{
			ENetEvent event;

			// Process all events
			while (enet_host_service(server, &event, 0) > 0)
			{
				switch (event.type)
				{
				case ENET_EVENT_TYPE_CONNECT:
				{
					g_logger_info("A new client connected from %x:%u.", event.peer->address.host, event.peer->address.port);
					// Store any relevant client information here...
					event.peer->data = (void*)"Client information";
					clients[numConnectedClients] = event.peer;
					numConnectedClients++;
					g_logger_assert(numConnectedClients <= 32, "Somehow we connected more than the maximum number of clients allowed.");

					g_logger_info("Sending client chunk data.");
					robin_hood::unordered_node_map<glm::ivec2, Chunk>& chunks = ChunkManager::getAllChunks();
					Network::sendClient(event.peer, NetworkEventType::WorldSeed, &World::seed, sizeof(uint32));
					uint16 numChunks = (uint16)chunks.size();
					size_t chunkDataSize = sizeof(Block) * World::ChunkHeight * World::ChunkWidth * World::ChunkDepth;
					size_t chunkCoordsSize = sizeof(int32) * 2;
					size_t chunkStateSize = sizeof(ChunkState);
					size_t chunkCompressedSizeSize = sizeof(uint32);
					uint8* chunkDataEvent = (uint8*)g_memory_allocate(sizeof(uint16) + (chunkDataSize * numChunks) + (chunkCoordsSize * numChunks) + (chunkStateSize * numChunks));
					g_logger_info("Num chunks: %u", numChunks);
					g_memory_copyMem(chunkDataEvent, &numChunks, sizeof(uint16));
					uint8* chunkDataPtr = chunkDataEvent + sizeof(uint16);
					bool first = true;
					for (auto& pair : chunks)
					{
						uint32 compressedChunkSize = 0;
						Chunk& chunk = pair.second;
						if (chunk.state == ChunkState::Loaded)
						{
							// Compressed chunk looks like this
							// NumChunks -> ChunkSize (uint16) -> blockId (uint16) -> blockCount(uint16) -> ... ...
							//		   -> blockId(uint16) -> blockCount(uint16)
							//         -> chunkCoords (int32) * 2 -> chunkState (uint8)
							uint8* chunkDataCurrentPtr = chunkDataPtr + chunkCompressedSizeSize;
							uint16 lastBlockId = chunk.data[0].id;
							uint16 lastBlockCount = 1;
							for (uint32 i = 1; i < World::ChunkHeight * World::ChunkWidth * World::ChunkDepth; i++)
							{
								if (chunk.data[i].id != lastBlockId)
								{
									// Write to our memory
									g_memory_copyMem(chunkDataCurrentPtr, &lastBlockId, sizeof(uint16));
									chunkDataCurrentPtr += sizeof(uint16);
									g_memory_copyMem(chunkDataCurrentPtr, &lastBlockCount, sizeof(uint16));
									chunkDataCurrentPtr += sizeof(uint16);
									compressedChunkSize += (sizeof(uint16) * 2);

									// Set the next id
									lastBlockId = chunk.data[i].id;
									lastBlockCount = 0;
								}
								lastBlockCount++;
							}
							if (lastBlockCount > 0)
							{
								g_memory_copyMem(chunkDataCurrentPtr, &lastBlockId, sizeof(uint16));
								chunkDataCurrentPtr += sizeof(uint16);
								g_memory_copyMem(chunkDataCurrentPtr, &lastBlockCount, sizeof(uint16));
								chunkDataCurrentPtr += sizeof(uint16);
								compressedChunkSize += (sizeof(uint16) * 2);
							}
							g_memory_copyMem(chunkDataPtr, &compressedChunkSize, sizeof(uint32));
							chunkDataPtr += chunkCompressedSizeSize + compressedChunkSize;

							g_memory_copyMem(chunkDataPtr, &chunk.chunkCoords.x, sizeof(int32));
							chunkDataPtr += sizeof(int32);
							g_memory_copyMem(chunkDataPtr, &chunk.chunkCoords.y, sizeof(int32));
							chunkDataPtr += sizeof(int32);
							g_memory_copyMem(chunkDataPtr, &chunk.state, sizeof(ChunkState));
							chunkDataPtr += sizeof(ChunkState);
						}
					}
					size_t totalCompressedSize = chunkDataPtr - chunkDataEvent;
					g_logger_info("Total compressed chunk data size: %u bytes", totalCompressedSize);
					Network::sendClient(event.peer, NetworkEventType::ChunkData, chunkDataEvent, totalCompressedSize);
					g_memory_free(chunkDataEvent);

					g_logger_info("Telling client to patch their dang chunk neighbors.");
					Network::sendClient(event.peer, NetworkEventType::PatchChunkNeighbors, nullptr, 0);
					Network::sendClient(event.peer, NetworkEventType::NotifyChunkWorker, nullptr, 0);

					Ecs::Registry* registry = Scene::getRegistry();
					RawMemory entityMemory = registry->serialize();
					Network::sendClient(event.peer, NetworkEventType::EntityData, entityMemory.data, entityMemory.size);
					g_memory_free(entityMemory.data);
					enet_host_flush(server);
					break;
				}
				case ENET_EVENT_TYPE_RECEIVE:
				{
					g_logger_info("A packet of length %u containing %s was received from %s on channel %u.",
						event.packet->dataLength,
						event.packet->data,
						event.peer->data,
						event.channelID);
					NetworkEventData networkEventData = Network::deserializeNetworkEvent(event.packet->data, event.packet->dataLength);
					processEvent(networkEventData.event, networkEventData.data);

					// Clean up the packet now that we're done using it
					enet_packet_destroy(event.packet);
					break;
				}
				case ENET_EVENT_TYPE_DISCONNECT:
				{
					g_logger_info("%s disconnected.", event.peer->data);

					bool foundClient = false;
					for (int i = 0; i < clients.size(); i++)
					{
						if (clients[i] == event.peer)
						{
							foundClient = true;
							clients[i] = clients[numConnectedClients - 1];
							clients[numConnectedClients - 1] = NULL;
							numConnectedClients--;
							break;
						}
					}

					// Reset the peer's client information
					event.peer->data = NULL;
					break;
				}
				}
			}
		}

		void broadcast(ENetPacket* packet)
		{
			enet_host_broadcast(server, 0, packet);
		}

		void sendClient(ENetPeer* peer, ENetPacket* packet)
		{
			if (enet_peer_send(peer, 0, packet) != 0)
			{
				g_logger_error("Failed to send packet from server.");
			}
		}

		void free()
		{
			enet_host_destroy(server);

			hostname = "";
			port = 0;
		}

		static void processEvent(NetworkEvent* event, uint8* data)
		{
			switch (event->type)
			{
			case NetworkEventType::Chat:
			{
				char* msg = (char*)data;
				g_logger_info("<ServerMsg>: %s", msg);
				break;
			}
			default:
			{
				g_logger_error("Unknown chat NetworkEventType: %d", event->type);
				break;
			}
			}
		}
	}
}