#ifndef MINECRAFT_CHUNK_MANAGER_H
#define MINECRAFT_CHUNK_MANAGER_H
#include "core.h"
#include "core/Pool.hpp"
#include "world/World.h"

namespace Minecraft
{
	struct Shader;
	struct Block;
	class Frustum;

	enum class SubChunkState : uint8
	{
		Unloaded,
		LoadBlockData,
		LoadingBlockData,
		RetesselateVertices,
		DoneRetesselating,
		TesselateVertices,
		TesselatingVertices,
		UploadVerticesToGpu,
		Uploaded
	};

	struct Vertex
	{
		uint32 data1;
		uint32 data2;
	};

	struct SubChunk
	{
		Vertex* data;
		uint32 first;
		uint32 drawCommandIndex;
		uint8 subChunkLevel;
		glm::ivec2 chunkCoordinates;
		std::atomic<uint32> numVertsUsed;
		std::atomic<SubChunkState> state;
	};

	// TODO: Make this internal to ChunkManager.cpp and remove includes above
	namespace Chunk
	{
		void generate(Block* blockData, const glm::ivec2& chunkCoordinates, int32 seed);
		// Must guarantee at least 16 sub-chunks located at this address
		void generateRenderData(Pool<SubChunk, World::ChunkCapacity * 16>* subChunks, const Block* blockData, const glm::ivec2& chunkCoordinates);

		Block getLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, const Block* blockData);
		Block getBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, const Block* blockData);
		bool setLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, Block* blockData, Block newBlock);
		bool setBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, Block* blockData, Block newBlock);
		bool removeLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, Block* blockData);
		bool removeBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, Block* blockData);

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
		void setBlock(const glm::vec3& worldPosition, Block newBlock);
		void removeBlock(const glm::vec3& worldPosition);

		void queueCreateChunk(const glm::ivec2& chunkCoordinates, bool retesselate);
		void render(const glm::vec3& playerPosition, const glm::ivec2& playerPositionInChunkCoords, Shader& shader, const Frustum& cameraFrustum);
		void checkChunkRadius(const glm::vec3& playerPosition);
	}
}

#endif