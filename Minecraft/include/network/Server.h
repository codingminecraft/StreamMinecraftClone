#ifndef MINECRAFT_SERVER_H
#define MINECRAFT_SERVER_H

typedef struct _ENetPacket ENetPacket;
typedef struct _ENetPeer ENetPeer;

namespace Minecraft
{
	namespace Server
	{
		void init(const char* hostname, int port);

		void update();

		void broadcast(ENetPacket* packet);

		void sendClient(ENetPeer* peer, ENetPacket* packet);

		void free();

		constexpr int listeningPort = 73019;
	}
}

#endif