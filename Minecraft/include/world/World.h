#ifndef MINECRAFT_WORLD_H
#define MINECRAFT_WORLD_H
#include "world/Chunk.h"

namespace Minecraft
{
	namespace World
	{
		void init();

		void update(float dt);

		void cleanup();
	}
}

#endif