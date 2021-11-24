#ifndef MINECRAFT_NETWORK_H
#define MINECRAFT_NETWORK_H
#include "core.h"

namespace Minecraft
{
	enum class NetworkEventType : uint8
	{
		Chat,
	};

	struct NetworkEvent
	{
		NetworkEventType type;
		size_t dataSize;
	};

	struct NetworkEventData
	{
		NetworkEvent* event;
		uint8* data;
	};

	namespace Network
	{
		void init(bool isServer, const char* hostname, int port);

		// TODO: Move this into it's own thread worker and process all network
		// stuff on a dedicated thread
		void update(float dt);

		void sendServer(NetworkEventType eventType, void* data, size_t dataSizeInBytes);
		void sendClient(NetworkEventType eventType, void* data, size_t dataSizeInBytes);
		void broadcast(NetworkEventType eventType, void* data, size_t dataSizeInBytes);

		bool isLanServer();

		void free();

		NetworkEventData deserializeNetworkEvent(uint8* data, size_t dataSizeInBytes);
	}
}

#endif