#ifndef MINECRAFT_CLIENT_H
#define MINECRAFT_CLIENT_H

typedef struct _ENetPacket ENetPacket;

namespace Minecraft
{
	namespace Client
	{
		void init();

		void update(float dt);

		void sendServer(ENetPacket* packet);

		void free();
	}
}

#endif