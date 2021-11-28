#ifndef MINECRAFT_NETWORK_H
#define MINECRAFT_NETWORK_H
#include "core.h"

typedef struct _ENetPeer ENetPeer;

namespace Minecraft
{
	enum class NetworkEventType : uint8
	{
		Chat,
		ChunkData,
		PatchChunkNeighbors,
		NotifyChunkWorker,
		WorldSeed,
		EntityData
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
		void sendClient(ENetPeer* peer, NetworkEventType eventType, void* data, size_t dataSizeInBytes);
		void broadcast(NetworkEventType eventType, void* data, size_t dataSizeInBytes);

		bool isLanServer();
		bool isNetworkEnabled();

		void free();

		NetworkEventData deserializeNetworkEvent(uint8* data, size_t dataSizeInBytes);
	}
}

#endif