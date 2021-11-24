#include "network/Network.h"
#include "network/Server.h"
#include "network/Client.h"

#include <enet/enet.h>

namespace Minecraft
{
	struct NetworkPacket
	{
		uint8* data;
		size_t size;
	};

	namespace Network
	{
		// Internal variables
		static bool isServer;
		static bool isInitialized = false;

		// Internal functions
		static NetworkPacket createPacket(NetworkEventType eventType, void* data, size_t dataSizeInBytes);

		void init(bool inIsServer, const char* hostname, int port)
		{
			g_logger_assert(!isInitialized, "Cannot initailze the network code twice.");
			isServer = inIsServer;

			if (enet_initialize() != 0)
			{
				g_logger_assert(false,
					"An error occurred while initializing ENet.\nTODO: This doesn't effect single-player mode, "
					"add in a gameplay mode that doesn't require ENet.");
			}

			if (isServer)
			{
				Server::init(hostname, port);
			}
			else
			{
				Client::init(hostname, port);
			}

			isInitialized = true;
		}

		void update(float dt)
		{
			if (isInitialized)
			{
				if (isServer)
				{
					Server::update(dt);
				}
				else
				{
					Client::update(dt);
				}
			}
		}

		void sendServer(NetworkEventType eventType, void* data, size_t dataSizeInBytes)
		{
			g_logger_assert(!isServer, "Cannot send server a message from the server.");
			ENetPacket* packet = enet_packet_create(data, dataSizeInBytes, ENET_PACKET_FLAG_RELIABLE);
			Client::sendServer(packet);
		}

		void sendClient(ENetPeer* peer, NetworkEventType eventType, void* data, size_t dataSizeInBytes)
		{
			g_logger_assert(isServer, "Cannot send client a message from the client.");
			NetworkPacket networkPacket = createPacket(eventType, data, dataSizeInBytes);
			ENetPacket* packet = enet_packet_create(networkPacket.data, networkPacket.size, ENET_PACKET_FLAG_RELIABLE);
			Server::sendClient(peer, packet);
			g_memory_free(networkPacket.data);
		}

		void broadcast(NetworkEventType eventType, void* data, size_t dataSizeInBytes)
		{
			NetworkPacket networkPacket = createPacket(eventType, data, dataSizeInBytes);
			ENetPacket* packet = enet_packet_create(networkPacket.data, networkPacket.size, ENET_PACKET_FLAG_RELIABLE);

			if (isServer)
			{
				Server::broadcast(packet);
			}
			else
			{
				Client::sendServer(packet);
			}

			g_memory_free(networkPacket.data);
		}

		bool isLanServer()
		{
			return isServer;
		}

		bool isNetworkEnabled()
		{
			return isInitialized;
		}

		void free()
		{
			if (isInitialized)
			{
				if (isServer)
				{
					Server::free();
				}
				else
				{
					Client::free();
				}

				enet_deinitialize();
			}
		}

		NetworkEventData deserializeNetworkEvent(uint8* data, size_t dataSizeInBytes)
		{
			g_logger_assert(dataSizeInBytes >= sizeof(NetworkEvent), "Error, all event messages must be >= sizeof(NetworkEvent).");
			NetworkEvent* networkEvent = (NetworkEvent*)(data);
			g_logger_assert(dataSizeInBytes == sizeof(NetworkEvent) + networkEvent->dataSize, "Error, all event messages must be equal to sizeof(NetworkEvent) + dataSize");

			uint8* eventData = nullptr;
			if (networkEvent->dataSize > 0)
			{
				eventData = data + sizeof(NetworkEvent);
			}

			NetworkEventData res;
			res.data = eventData;
			res.event = networkEvent;
			return res;
		}

		// Internal functions
		static NetworkPacket createPacket(NetworkEventType eventType, void* data, size_t dataSizeInBytes)
		{
			// Copy event and data into the packet
			size_t eventPlusDataSize = sizeof(NetworkEvent) + dataSizeInBytes;
			// TODO: Create custom stack based memory allocator for the server messages
			uint8* eventPlusData = (uint8*)g_memory_allocate(eventPlusDataSize);
			NetworkEvent* networkEvent = (NetworkEvent*)eventPlusData;
			networkEvent->dataSize = dataSizeInBytes;
			networkEvent->type = eventType;

			if (dataSizeInBytes > 0)
			{
				g_memory_copyMem(eventPlusData + sizeof(NetworkEvent), data, dataSizeInBytes);
			}

			NetworkPacket res;
			res.data = eventPlusData;
			res.size = eventPlusDataSize;
			return res;
		}
	}
}
