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

	struct SubChunk
	{
		Vertex* data;
		uint32 vao;
		uint32 vbo;
		std::atomic<uint32> numVertsUsed;
		std::atomic<bool> loaded;
		std::atomic<bool> uploadVertsToGpu;
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
		glm::ivec2 chunkCoordinates;

		Block getLocalBlock(glm::ivec3 localPosition);
		void generate(Block* newChunkData, int chunkX, int chunkZ, int32 seed);

		// Must guarantee at least 16 sub-chunks located at this address
		void generateRenderData(SubChunk* subChunks);

		void serialize(const std::string& worldSavePath);
		void deserialize(Block* newChunkData, const std::string& worldSavePath, int chunkX, int chunkZ);

		static bool exists(const std::string& worldSavePath, int chunkX, int chunkZ);
		static void info();
	};
}

#endif 