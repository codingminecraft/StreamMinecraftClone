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
	struct Chunk;

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
		std::atomic<bool> isBlendable;
		std::atomic<uint32> numVertsUsed;
		std::atomic<SubChunkState> state;
	};

	namespace ChunkManager
	{
		void init();
		void free();
		void serialize();

		Block getBlock(const glm::vec3& worldPosition);
		void setBlock(const glm::vec3& worldPosition, Block newBlock);
		void removeBlock(const glm::vec3& worldPosition);

		Chunk* getChunk(const glm::vec3& worldPosition);
		Chunk* getChunk(const glm::ivec2& chunkCoords);

		void patchChunkPointers();

		void queueGenerateDecorations(const glm::ivec2& lastPlayerLoadChunkPos);
		void queueCalculateLighting(const glm::ivec2& lastPlayerPosInChunkCoords);
		void queueCreateChunk(const glm::ivec2& chunkCoordinates);
		void queueSaveChunk(const glm::ivec2& chunkCoordinates);
		void queueRecalculateLighting(const glm::ivec2& chunkCoordinates, const glm::vec3& blockPositionThatUpdated, bool removedLightSource);
		void queueRetesselateChunk(const glm::ivec2& chunkCoordinates, Chunk* chunk = nullptr, bool doImmediately = false);
		void render(const glm::vec3& playerPosition, const glm::ivec2& playerPositionInChunkCoords, Shader& opaqueShader, Shader& transparentShader, const Frustum& cameraFrustum);
		void checkChunkRadius(const glm::vec3& playerPosition);

		// TODO: Make this private
		void unloadChunk(const glm::ivec2& chunkCoordinates);

		extern bool doStepLogic;
	}
}

#endif