#ifndef MINECRAFT_CLIENT_H
#define MINECRAFT_CLIENT_H
#include "core.h"

typedef struct _ENetPacket ENetPacket;

namespace Minecraft
{
	namespace Client
	{
		void init(const char* hostname, int port);

		void update();

		void sendServer(ENetPacket* packet);

		void free();

		extern uint64 clientGameTime;
	}
}

#endif