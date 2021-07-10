#pragma once

#include "core.h"

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
	};

	struct Chunk
	{
		int16* chunkData;

		ChunkRenderData generate();
	};

	namespace BlockMap
	{
		
	}
}