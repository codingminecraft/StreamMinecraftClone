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

	struct RenderState
	{
		uint32 vao;
		uint32 vbo;
	};

	struct ChunkRenderData
	{
		Vertex* vertices;
		int32* elements;

		size_t vertexSizeBytes;
		size_t elementSizeBytes;
		uint32 numElements;

		RenderState renderState;
	};

	// 64 bits per block 
	// 16 bit integer id 2^16
	// 4 bits light level 0-15
	// 4 bits rotation direction 
	// 8 bits block type
	// 32 bits extra stuff

	struct Block
	{
		int16 id;
		int8 lightLevel;
		int8 rotation;
		int32 padding;
	};

	bool operator==(const Block& a, const Block& b);

	struct Chunk
	{
		Block* chunkData;
		ChunkRenderData renderData;
		glm::ivec2 worldPosition;

		void generate(int chunkX, int chunkZ, int32 seed);

		void render();
	};
}

#endif 