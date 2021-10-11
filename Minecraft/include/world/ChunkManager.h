#ifndef MINECRAFT_CHUNK_MANAGER_H
#define MINECRAFT_CHUNK_MANAGER_H
#include "core.h"

namespace Minecraft
{
	struct Chunk;
	struct Shader;

	namespace ChunkManager
	{
		void init(uint32 worldSeed);
		void free();

		void queueCreateChunk(int32 x, int32 z);
		Chunk getChunk(const glm::vec3& worldPosition);

		void render(const glm::vec3& playerPosition, const glm::ivec2& playerPositionInChunkCoords, Shader& shader);

	}
}

#endif