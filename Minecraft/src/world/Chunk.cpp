#include "world/Chunk.hpp"
#include "world/World.h"
#include "world/BlockMap.h"

namespace Minecraft
{
	RawMemory Chunk::serialize() const
	{
		RawMemory res;
		res.data = nullptr;
		res.size = 0;

		if (state == ChunkState::Saving)
		{
			res.init(sizeof(Block) * World::ChunkWidth & World::ChunkHeight * World::ChunkDepth);

			res.setCursor(sizeof(uint32));

			// Compressed chunk looks like this
			// NumChunks -> ChunkSize (uint16) -> blockId (uint16) -> blockCount(uint16) -> ... ...
			//		   -> blockId(uint16) -> blockCount(uint16)
			//         -> chunkCoords (int32) * 2 -> chunkState (uint8)
			uint16 lastBlockId = data[0].id;
			uint16 lastBlockCount = 1;
			for (uint32 i = 1; i < World::ChunkHeight * World::ChunkWidth * World::ChunkDepth; i++)
			{
				if (data[i].id != lastBlockId)
				{
					// Write to our memory
					res.write<uint16>(&lastBlockId);
					res.write<uint16>(&lastBlockCount);

					// Set the next id
					lastBlockId = data[i].id;
					lastBlockCount = 0;
				}
				lastBlockCount++;
			}
			if (lastBlockCount > 0)
			{
				res.write<uint16>(&lastBlockId);
				res.write<uint16>(&lastBlockCount);
			}
			size_t lastOffset = res.offset;
			uint32 compressedChunkSize = (uint32)(lastOffset - sizeof(uint32));

			// Write whatever the size is at the beginning of the memory block
			res.setCursor(0);
			res.write<uint32>(&compressedChunkSize);
			res.setCursor(lastOffset);

			res.write<int32>(&chunkCoords.x);
			res.write<int32>(&chunkCoords.y);
			res.shrinkToFit();
		}

		return res;
	}

	void Chunk::deserialize(RawMemory& memory)
	{
		memory.resetReadWriteCursor();

		uint32 compressedChunkSize;
		memory.read<uint32>(&compressedChunkSize);
		g_memory_zeroMem(data, sizeof(Block) * World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);

		int blockIndex = 0;
		while (memory.offset < compressedChunkSize + sizeof(uint32))
		{
			uint16 blockId;
			uint16 blockCount;
			memory.read<uint16>(&blockId);
			memory.read<uint16>(&blockCount);

			g_logger_assert(blockIndex + blockCount <= World::ChunkWidth * World::ChunkDepth * World::ChunkHeight,
				"Encountered bad data while deserializing chunk data.");
			int maxBlockCount = glm::min((int)blockCount, World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);

			const BlockFormat& blockFormat = BlockMap::getBlock(blockId);
			for (int blockCounter = 0; blockCounter < maxBlockCount; blockCounter++)
			{
				data[blockIndex].id = blockId;
				data[blockIndex].setLightLevel(0);
				data[blockIndex].setSkyLightLevel(0);
				data[blockIndex].setTransparent(blockFormat.isTransparent);
				data[blockIndex].setIsBlendable(blockFormat.isBlendable);
				data[blockIndex].setIsLightSource(blockFormat.isLightSource);
				blockIndex++;
			}
		}
		g_logger_assert(blockIndex == World::ChunkWidth * World::ChunkDepth * World::ChunkHeight,
			"Deserialized invalid block data on client. Count was '%d', should be '%d'", blockIndex, World::ChunkWidth * World::ChunkHeight * World::ChunkDepth);
		
		int32 chunkX, chunkZ;
		memory.read<int32>(&chunkX);
		memory.read<int32>(&chunkZ);
		chunkCoords.x = chunkX;
		chunkCoords.y = chunkZ;
	}
}