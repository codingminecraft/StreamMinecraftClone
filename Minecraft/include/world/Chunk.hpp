#ifndef MINECRAFT_CHUNK_H
#define MINECRAFT_CHUNK_H
#include "core.h"

namespace Minecraft
{
	struct Block;

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
}

#endif