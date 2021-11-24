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

		static const char* hostname = "127.0.0.1";
		static int port = 8080;

		void init()
		{
			// Bind the server to the default localhost
			address.host = ENET_HOST_ANY;
			address.port = port;

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
					g_logger_info("A new client connected from %x:%u.", event.peer->address.host, event.peer->address.port);
					// Store any relevant client information here...
					event.peer->data = "Client information";
					break;
				case ENET_EVENT_TYPE_RECEIVE:
					g_logger_info("A packet of length %u containing %s was received from %s on channel %u.",
						event.packet->dataLength,
						event.packet->data,
						event.peer->data,
						event.channelID);

					// Clean up the packet now that we're done using it
					enet_packet_destroy(event.packet);
					break;
				case ENET_EVENT_TYPE_DISCONNECT:
					g_logger_info("%s disconnected.", event.peer->data);

					// Reset the peer's client information
					event.peer->data = NULL;
					break;
				}
			}
		}

		void free()
		{
			enet_host_destroy(server);
		}
	}
}