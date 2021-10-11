#ifndef MINECRAFT_CHUNK_MANAGER_H
#define MINECRAFT_CHUNK_MANAGER_H
#include "core.h"

namespace Minecraft
{
	struct Shader;
	struct Block;

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
		glm::ivec2 chunkCoordinates;
		std::atomic<uint32> numVertsUsed;
		std::atomic<bool> loaded;
		std::atomic<bool> uploadVertsToGpu;
	};

	namespace Chunk
	{
		void generate(Block* blockData, const glm::ivec2& chunkCoordinates, int32 seed);
		// Must guarantee at least 16 sub-chunks located at this address
		void generateRenderData(SubChunk* subChunks, const Block* blockData, const glm::ivec2& chunkCoordinates);

		Block getLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, const Block* blockData);
		Block getBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, const Block* blockData);

		void serialize(const std::string& worldSavePath, const Block* blockData, const glm::ivec2& chunkCoordinates);
		void deserialize(Block* blockData, const std::string& worldSavePath, const glm::ivec2& chunkCoordinates);

		bool exists(const std::string& worldSavePath, const glm::ivec2& chunkCoordinates);
		void info();
	}

	namespace ChunkManager
	{
		void init(uint32 worldSeed);
		void free();

		Block getBlock(const glm::vec3& worldPosition);

		void queueCreateChunk(int32 x, int32 z);
		void render(const glm::vec3& playerPosition, const glm::ivec2& playerPositionInChunkCoords, Shader& shader);

	}
}

#endif