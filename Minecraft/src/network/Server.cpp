#include "network/Server.h"
#include "core.h"
#include "network/Network.h"

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

		static const char* hostname = "127.0.0.1";
		static int port = 8080;

		// Internal functions
		static void processEvent(NetworkEvent* event, uint8* data);

		void init()
		{
			// Bind the server to the default localhost
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
					event.peer->data = "Client information";
					clients[numConnectedClients] = event.peer;
					numConnectedClients++;
					g_logger_assert(numConnectedClients <= 32, "Somehow we connected more than the maximum number of clients allowed.");
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

		void sendClient(ENetPacket* packet)
		{
			// TODO: Create client abstraction to send messages to individual clients
			//enet_peer_send(peer, 0, packet);
		}

		void free()
		{
			enet_host_destroy(server);
		}

		static void processEvent(NetworkEvent* event, uint8* data)
		{
			switch (event->type)
			{
			case NetworkEventType::Chat:
				char* msg = (char*)data;
				g_logger_info("<ServerMsg>: %s", msg);
				break;
			}
		}
	}
}