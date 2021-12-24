#ifndef MINECRAFT_CHUNK_H
#define MINECRAFT_CHUNK_H
#include "core.h"
// TODO: Remove this by getting rid of Pool type
#include "world/ChunkManager.h"
#include "core/Pool.hpp"

namespace Minecraft
{
	struct Block;
	struct SubChunk;

	enum class ChunkState : uint8
	{
		None,
		Unloaded,
		Unloading,
		Saving,
		Loading,
		Loaded
	};

	struct Chunk
	{
		Block* data;
		glm::ivec2 chunkCoords;
		ChunkState state;
		bool needsToGenerateDecorations;
		bool needsToCalculateLighting;

		Chunk* topNeighbor;
		Chunk* bottomNeighbor;
		Chunk* leftNeighbor;
		Chunk* rightNeighbor;

		inline bool operator==(const Chunk& other) const
		{
			return chunkCoords == other.chunkCoords;
		}

		inline bool operator!=(const Chunk& other) const
		{
			return !(*this == other);
		}

		inline bool operator==(const glm::ivec2& other) const
		{
			return chunkCoords == other;
		}

		inline bool operator!=(const glm::ivec2& other) const
		{
			return !(*this == other);
		}

		RawMemory serialize() const;
		void deserialize(RawMemory& memory);

		struct HashFunction
		{
			inline std::size_t operator()(const Chunk& key) const
			{
				return std::hash<int>()(key.chunkCoords.x) ^
					std::hash<int>()(key.chunkCoords.y);
			}
		};
	};

	namespace ChunkPrivate
	{
		void generateTerrain(Chunk* chunk, const glm::ivec2& chunkCoordinates, float seed);
		void generateDecorations(const glm::ivec2& lastPlayerLoadPosChunkCoords, float seed);
		// Must guarantee at least 16 sub-chunks located at this address
		void generateRenderData(Pool<SubChunk>* subChunks, const Chunk* chunk, const glm::ivec2& chunkCoordinates, bool isRetesselation=false);
		void calculateLighting(const glm::ivec2& lastPlayerLoadPosChunkCoords);
		void calculateLightingUpdate(Chunk* chunk, const glm::ivec2& chunkCoordinates, const glm::vec3& blockPosition, bool removedLightSource, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate);

		Block getLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, const Chunk* blockData);
		Block getBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, const Chunk* blockData);
		bool setLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, Chunk* blockData, Block newBlock);
		bool setBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, Chunk* blockData, Block newBlock);
		bool removeLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, Chunk* blockData);
		bool removeBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, Chunk* blockData);

		void serialize(const std::string& worldSavePath, const Chunk& chunk);
		void deserialize(Chunk& blockData, const std::string& worldSavePath);

		bool exists(const std::string& worldSavePath, const glm::ivec2& chunkCoordinates);
		void info();
	}
}

#endif