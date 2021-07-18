#ifndef MINECRAFT_CHUNK_H
#define MINECRAFT_CHUNK_H
#include "core.h"

#include <unordered_map>
#include <string>

namespace Minecraft
{
	struct Vertex
	{
		glm::vec3 position;
		glm::vec2 uv;
	};

	struct ChunkRenderData
	{
		Vertex* vertices;
		int32* elements;

		size_t vertexSizeBytes;
		size_t elementSizeBytes;
		uint32 numElements;
	};

	struct Chunk
	{
		int16* chunkData;

		ChunkRenderData generate();
	};
}

#endif 