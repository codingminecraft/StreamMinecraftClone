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
		EntityData,
		LocalPlayer,
		UserCommand,
	};

	enum class UserCommandType : uint8
	{
		UpdatePosition,
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

	struct UserCommand
	{
		UserCommandType type;
		uint64 timestamp;
		size_t sizeOfData;
	};

	namespace Network
	{
		void init(bool isServer, const char* hostname, int port);

		// TODO: Move this into it's own thread worker and process all network
		// stuff on a dedicated thread
		void update();

		void sendServer(NetworkEventType eventType, void* data, size_t dataSizeInBytes, bool isReliable = true);
		void sendClient(ENetPeer* peer, NetworkEventType eventType, void* data, size_t dataSizeInBytes, bool isReliable = true);
		void broadcast(NetworkEventType eventType, void* data, size_t dataSizeInBytes, bool isReliable = true);

		// User Commands
		void sendUserCommand(UserCommandType type, void* data, size_t dataSizeInBytes);

		bool isLanServer();
		bool isNetworkEnabled();

		void free();

		NetworkEventData deserializeNetworkEvent(uint8* data, size_t dataSizeInBytes);
	}
}

#endif