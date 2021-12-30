#ifndef MINECRAFT_CLIENT_H
#define MINECRAFT_CLIENT_H
#include "core.h"

typedef struct _ENetPacket ENetPacket;
typedef struct _ENetAddress ENetAddress;

namespace Minecraft
{
	namespace Client
	{
		void init();

		void update();

		void sendServer(ENetPacket* packet);

		void setAddress(const ENetAddress& address);

		bool isConnecting();

		void free();

		extern uint64 clientGameTime;
	}
}

#endif