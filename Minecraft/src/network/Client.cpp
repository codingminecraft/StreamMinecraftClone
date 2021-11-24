#include "network/Client.h"
#include "core.h"
#include "network/Network.h"
#include "world/ChunkManager.h"
#include "world/Chunk.hpp"
#include "world/BlockMap.h"

#include <enet/enet.h>

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
			if (enet_host_service(client, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
			{
				g_logger_info("Successfully connected to '%s':'%d'", hostname, port);
				event.peer->data = "Client Info Blah";
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
					event.peer->data = "Client information";
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
				size_t chunkDataSize = sizeof(Block) * World::ChunkHeight * World::ChunkWidth * World::ChunkDepth;
				size_t chunkCoordsSize = sizeof(int) * 2;
				size_t chunkStateSize = sizeof(ChunkState);
				int chunkX = *(int*)(char*)(data + chunkDataSize);
				int chunkY = *(int*)(char*)(data + chunkDataSize + sizeof(int));
				ChunkState state = *(ChunkState*)((char*)(data + chunkDataSize + chunkCoordsSize));
				void* chunkData = g_memory_allocate(chunkDataSize);
				g_memory_copyMem(chunkData, data, chunkDataSize);
				ChunkManager::queueClientLoadChunk(chunkData, glm::ivec2(chunkX, chunkY), state);
				break;
			}
			case NetworkEventType::PatchChunkNeighbors:
			{
				ChunkManager::patchChunkPointers();
				break;
			}
			case NetworkEventType::NotifyChunkWorker:
			{
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
			default:
			{
				g_logger_error("Unknown chat NetworkEventType: %d", event->type);
				break;
			}
			}
		}
	}
}
