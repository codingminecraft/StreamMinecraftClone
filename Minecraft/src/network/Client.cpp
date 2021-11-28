#include "network/Client.h"
#include "core.h"
#include "core/Scene.h"
#include "network/Network.h"
#include "world/ChunkManager.h"
#include "world/Chunk.hpp"
#include "world/BlockMap.h"

#include <enet/enet.h>

#undef min
namespace Minecraft
{
	namespace Client
	{
		// Internal variables
		static ENetAddress address;
		static ENetHost* client;
		static ENetPeer* peer;

		static const char* hostname;
		static int port;

		// Internal functions
		static void processEvent(NetworkEvent* event, uint8* data);

		void init(const char* inHostname, int inPort)
		{
			client = enet_host_create(
				NULL, // create a client host
				1, // only allow 1 outgoing connection
				2, // allow up 2 channels to be used, 0 and 1
				0, // assume any amount of incoming bandwidth
				0); // assume any amount of outgoing bandwidth

			if (client == NULL)
			{
				g_logger_assert(false, "An error occurred while trying to create an ENet client host.");
				return;
			}

			// Bind the client to the default localhost
			g_logger_assert(strcmp(inHostname, "") != 0 && inPort != 0, "Need to supply hostname and port to server initialization.");
			hostname = inHostname;
			port = inPort;

			enet_address_set_host(&address, hostname);
			address.port = port;

			// Create a peer that is connected to the server
			peer = enet_host_connect(client, &address, 1, 0);
			if (peer == NULL)
			{
				g_logger_assert(false, "No available peers for initiating an ENet connection.");
				return;
			}

			// Check if the connection succeeded
			ENetEvent event;
			if (enet_host_service(client, &event, 15000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
			{
				g_logger_info("Successfully connected to '%s':'%d'", hostname, port);
				event.peer->data = (void*)"Client Info Blah";
			}
			else
			{
				enet_peer_reset(peer);
				g_logger_error("Failed to connect to '%s':'%d'", hostname, port);
				peer = NULL;
				free();
				return;
			}
		}

		void update(float dt)
		{
			ENetEvent event;

			// Process all events
			while (enet_host_service(client, &event, 0) > 0)
			{
				switch (event.type)
				{
				case ENET_EVENT_TYPE_CONNECT:
				{
					g_logger_info("A new client connected from %x:%u.", event.peer->address.host, event.peer->address.port);
					// Store any relevant client information here...
					event.peer->data = (void*)"Client information";
					break;
				}
				case ENET_EVENT_TYPE_RECEIVE:
				{
					NetworkEventData networkEventData = Network::deserializeNetworkEvent(event.packet->data, event.packet->dataLength);
					processEvent(networkEventData.event, networkEventData.data);

					// Clean up the packet now that we're done using it
					enet_packet_destroy(event.packet);
					break;
				}
				case ENET_EVENT_TYPE_DISCONNECT:
				{
					g_logger_info("%s disconnected.", event.peer->data);

					// Reset the peer's client information
					event.peer->data = NULL;
					break;
				}
				}
			}
		}

		void sendServer(ENetPacket* packet)
		{
			enet_peer_send(peer, 0, packet);
		}

		void free()
		{
			enet_peer_disconnect(peer, 0);
			peer = NULL;

			// Process all the events
			update(0.16f);

			enet_host_destroy(client);
			client = NULL;

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
				g_logger_info("<ClientMsg>: %s", msg);
				break;
			}
			case NetworkEventType::ChunkData:
			{
				g_logger_info("Recieving server chunk data.");
				uint16 numChunks;
				uint8* chunkDataPtr = data;
				g_memory_copyMem(&numChunks, chunkDataPtr, sizeof(uint16));
				chunkDataPtr += sizeof(uint16);
				g_logger_info("Num chunks: %u", numChunks);

				for (uint16 i = 0; i < numChunks; i++)
				{
					uint32 compressedChunkSize;
					g_memory_copyMem(&compressedChunkSize, chunkDataPtr, sizeof(uint32));
					chunkDataPtr += sizeof(uint32);

					Block* chunkData = (Block*)g_memory_allocate(sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth);
					g_memory_zeroMem(chunkData, sizeof(Block) * World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);
					int blockIndex = 0;
					uint32 chunkByteCounter = 0;
					while (chunkByteCounter < compressedChunkSize)
					{
						uint16 blockId;
						uint16 blockCount;
						g_memory_copyMem(&blockId, chunkDataPtr, sizeof(uint16));
						chunkDataPtr += sizeof(uint16);
						chunkByteCounter += sizeof(uint16);
						g_memory_copyMem(&blockCount, chunkDataPtr, sizeof(uint16));
						chunkDataPtr += sizeof(uint16);
						chunkByteCounter += sizeof(uint16);

						g_logger_assert(blockIndex + blockCount <= World::ChunkWidth * World::ChunkDepth * World::ChunkHeight,
							"Encountered bad data while serializing chunk data.");
						int maxBlockCount = glm::min((int)blockCount, World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);
						for (int blockCounter = 0; blockCounter < maxBlockCount; blockCounter++)
						{
							chunkData[blockIndex].id = blockId;
							blockIndex++;
						}
					}
					g_logger_assert(blockIndex == World::ChunkWidth * World::ChunkDepth * World::ChunkHeight, 
						"Deserialized invalid block data on client. Count was '%d', should be '%d'", blockIndex, World::ChunkWidth * World::ChunkHeight * World::ChunkDepth);
					int32 chunkX, chunkZ;
					ChunkState state;
					g_memory_copyMem(&chunkX, chunkDataPtr, sizeof(int32));
					chunkDataPtr += sizeof(int32);
					g_memory_copyMem(&chunkZ, chunkDataPtr, sizeof(int32));
					chunkDataPtr += sizeof(int32);
					g_memory_copyMem(&state, chunkDataPtr, sizeof(ChunkState));
					chunkDataPtr += sizeof(ChunkState);

					ChunkManager::queueClientLoadChunk(chunkData, glm::ivec2(chunkX, chunkZ), state);
				}
				g_logger_assert(chunkDataPtr - data == event->dataSize, "Deserialized more or less data than we recieved from the client.");
				g_logger_info("Done processing chunk data.");
				break;
			}
			case NetworkEventType::PatchChunkNeighbors:
			{
				g_logger_info("Patching chunk pointers.");
				ChunkManager::patchChunkPointers();
				g_logger_info("Done patching chunk pointers.");
				break;
			}
			case NetworkEventType::NotifyChunkWorker:
			{
				g_logger_info("Starting the work on the thread.");
				ChunkManager::beginWork();
				break;
			}
			case NetworkEventType::WorldSeed:
			{
				uint32 worldSeed = *(uint32*)(char*)(data);
				World::seed = worldSeed;
				World::seedAsFloat = (float)((double)worldSeed / (double)UINT32_MAX) * 2.0f - 1.0f;
				g_logger_info("Client received world seed: %u", worldSeed);
				g_logger_info("World seed (as float): %2.8f", World::seedAsFloat.load());
				break;
			}
			case NetworkEventType::EntityData:
			{
				Ecs::Registry* registry = Scene::getRegistry();
				RawMemory memory;
				memory.data = data;
				memory.size = event->dataSize;
				registry->deserialize(memory);
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
