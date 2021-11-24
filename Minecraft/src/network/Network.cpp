#include "network/Network.h"
#include "network/Server.h"
#include "network/Client.h"

#include <enet/enet.h>

namespace Minecraft
{
	namespace Network
	{
		// Internal variables
		static bool isServer;
		static bool isClient;

		void init(bool inIsServer, bool inIsClient)
		{
			isServer = inIsServer;
			isClient = inIsClient;

			g_logger_assert(!(isServer && isServer == isClient), "You cannot start a server and a client in the same application.");

			if (enet_initialize() != 0)
			{
				g_logger_assert(false,
					"An error occurred while initializing ENet.\nTODO: This doesn't effect single-player mode, "
					"add in a gameplay mode that doesn't require ENet.");
			}

			// We aren't doing any networking stuff here...
			if (!isServer && !isClient)
			{
				return;
			}

			if (isServer)
			{
				Server::init();
			}

			if (isClient)
			{
				Client::init();
			}
		}

		void update(float dt)
		{
			if (isServer)
			{
				Server::update(dt);
			}

			if (isClient)
			{
				Client::update(dt);
			}
		}

		void sendServer(NetworkEventType eventType, void* data, size_t dataSizeInBytes)
		{
			g_logger_assert(isClient, "Cannot send server a message from the server.");
			ENetPacket* packet = enet_packet_create(data, dataSizeInBytes, ENET_PACKET_FLAG_RELIABLE);
			Client::sendServer(packet);
		}

		void sendClient(NetworkEventType eventType, void* data, size_t dataSizeInBytes)
		{
			g_logger_assert(isServer, "Cannot send client a message from the client.");
		}

		void broadcast(NetworkEventType eventType, void* data, size_t dataSizeInBytes)
		{
			// Copy event and data into the packet
			size_t eventPlusDataSize = sizeof(NetworkEvent) + dataSizeInBytes;
			// TODO: Create custom stack based memory allocator for the server messages
			uint8* eventPlusData = (uint8*)g_memory_allocate(eventPlusDataSize);
			NetworkEvent* networkEvent = (NetworkEvent*)eventPlusData;
			networkEvent->dataSize = dataSizeInBytes;
			networkEvent->type = eventType;
			g_memory_copyMem(eventPlusData + sizeof(NetworkEvent), data, dataSizeInBytes);

			ENetPacket* packet = enet_packet_create(eventPlusData, eventPlusDataSize, ENET_PACKET_FLAG_RELIABLE);

			if (isServer)
			{
				Server::broadcast(packet);
			}
			else if (isClient)
			{
				Client::sendServer(packet);
			}
			g_memory_free(eventPlusData);
		}

		void free()
		{
			if (isClient)
			{
				Client::free();
			}

			if (isServer)
			{
				Server::free();
			}

			enet_deinitialize();
		}
	}
}
