#ifndef MINECRAFT_CHUNK_H
#define MINECRAFT_CHUNK_H
#include "core.h"

namespace Minecraft
{
	struct Vertex
	{
		uint32 data1;
		uint32 data2;
	};

	struct RenderState
	{
		uint32 vao;
		uint32 vbo;
	};

	struct ChunkRenderData
	{
		Vertex* vertices;
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
		glm::ivec2 chunkCoordinates;
		uint32 numVertices;
		bool loaded;

		void generate(int chunkX, int chunkZ, int32 seed);
		void generateRenderData();
		void uploadToGPU();
		void render() const;

		void serialize(const std::string& worldSavePath);
		void deserialize(const std::string& worldSavePath, int chunkX, int chunkZ);

		void freeCpu();
		void freeGpu();

		static bool exists(const std::string& worldSavePath, int chunkX, int chunkZ);
		static void info();
	};
}

#endif 