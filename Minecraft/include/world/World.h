#ifndef MINECRAFT_WORLD_H
#define MINECRAFT_WORLD_H
#include "core.h"
#include "Chunk.h"
#include "core/Ecs.h"

namespace Minecraft
{
	struct Camera;

	namespace World
	{
		void init(Ecs::Registry& registry);
		void free();
		void update(float dt);

		glm::ivec2 toChunkCoords(const glm::vec3& worldCoordinates);
		Block getBlock(const glm::vec3& worldPosition);
		Chunk& getChunk(const glm::vec3& worldPosition);

		// Area of circle is PI * r^2, we'll round PI up to 4
		const uint16 ChunkRadius = 8;
		const uint16 ChunkCapacity = (ChunkRadius + 1) * (ChunkRadius + 1) * 4;
	}
}

#endif