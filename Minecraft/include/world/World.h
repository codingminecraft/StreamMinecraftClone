#ifndef MINECRAFT_WORLD_H
#define MINECRAFT_WORLD_H
#include "world/Chunk.h"

namespace Minecraft
{
	struct PlayerController;

	namespace World
	{
		void init();

		void update(float dt);

		void cleanup();

		// Area of circle is PI * r^2, we'll round PI up to 4
		const uint16 ChunkRadius = 16;
		const uint16 ChunkCapacity = (ChunkRadius + 1) * (ChunkRadius + 1) * 4;
	}
}

#endif