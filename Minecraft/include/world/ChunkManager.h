#ifndef MINECRAFT_CHUNK_MANAGER_H
#define MINECRAFT_CHUNK_MANAGER_H
#include "core.h"

namespace Minecraft
{
	struct Chunk;

	namespace ChunkManager
	{
		void init(uint32 worldSeed);
		void free();

		void queueCreateChunk(int32 x, int32 z);
		void queueDeleteChunk(const Chunk& chunk);

		std::vector<Chunk> getReadyChunks();
	}
}

#endif