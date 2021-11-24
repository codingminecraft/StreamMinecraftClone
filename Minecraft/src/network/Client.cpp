#include "network/Client.h"
#include "core.h"
#include "network/Network.h"

#include <enet/enet.h>

namespace Minecraft
{
	namespace Client
	{
		// Internal variables
		static ENetAddress address;
		static ENetHost* client;
		static ENetPeer* peer;

		static const char* hostname = "127.0.0.1";
		static int port = 8080;

		// Internal functions
		static void processEvent(NetworkEvent* event, uint8* data);

		void init()
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
					g_logger_info("A packet of length %u containing %s was received from %s on channel %u.",
						event.packet->dataLength,
						event.packet->data,
						event.peer->data,
						event.channelID);
					g_logger_assert(event.packet->dataLength >= sizeof(NetworkEvent), "Error, all event messages must be >= sizeof(NetworkEvent).");
					NetworkEvent* networkEvent = (NetworkEvent*)(event.packet->data);
					g_logger_assert(event.packet->dataLength == sizeof(NetworkEvent) + networkEvent->dataSize, "Error, all event messages must be equal to sizeof(NetworkEvent) + dataSize");
					uint8* data = event.packet->data + sizeof(NetworkEvent);
					processEvent(networkEvent, data);

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
		}

		static void processEvent(NetworkEvent* event, uint8* data)
		{
			switch (event->type)
			{
			case NetworkEventType::Chat:
				char* msg = (char*)data;
				g_logger_info("<ClientMsg>: %s", msg);
				break;
			}
		}
	}
}
