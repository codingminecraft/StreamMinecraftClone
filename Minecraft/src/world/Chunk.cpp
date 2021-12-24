#include "world/Chunk.hpp"
#include "world/World.h"
#include "world/BlockMap.h"
#include "world/ChunkManager.h"
#include "world/TerrainGenerator.h"
#include "utils/Constants.h"
#include "utils/DebugStats.h"
#include "network/Network.h"
#include "core/File.h"

#include <xmmintrin.h>

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
				data[blockIndex].setLightColor(glm::ivec3(255, 255, 255));
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

	// ==================================================
	// 	   Chunk Private
	// ==================================================
	namespace ChunkPrivate
	{
		// Internal Enums
		enum class CUBE_FACE : uint32
		{
			LEFT = 0,
			RIGHT = 1,
			BOTTOM = 2,
			TOP = 3,
			BACK = 4,
			FRONT = 5,
			SIZE = 6
		};

		enum class UV_INDEX : uint32
		{
			TOP_RIGHT = 0,
			TOP_LEFT = 1,
			BOTTOM_LEFT = 2,
			BOTTOM_RIGHT = 3,
			SIZE
		};

		// Internal Constants
		static const int POSITION_INDEX_BITMASK = 0x1FFFF;
		static const int TEX_ID_BITMASK = 0x1FFE0000;
		static const int FACE_BITMASK = 0xE0000000;

		static const int UV_INDEX_BITMASK = 0x3;
		static const int COLOR_BLOCK_BIOME_BITMASK = 0x4;
		static const int LIGHT_LEVEL_BITMASK = 0xF8;
		static const int LIGHT_COLOR_BITMASK_R = 0x00700;
		static const int LIGHT_COLOR_BITMASK_G = 0x03800;
		static const int LIGHT_COLOR_BITMASK_B = 0x1C000;
		static const int SKY_LIGHT_LEVEL_BITMASK = 0x3e0000;

		static const int BASE_17_DEPTH = 17;
		static const int BASE_17_WIDTH = 17;
		static const int BASE_17_HEIGHT = 289;

		// Internal functions
		static int to1DArray(int x, int y, int z);
		static Block getBlockInternal(const Chunk* chunk, int x, int y, int z);
		static bool setBlockInternal(Chunk* chunk, int x, int y, int z, Block newBlock);
		static bool removeBlockInternal(Chunk* chunk, int x, int y, int z);
		static std::string getFormattedFilepath(const glm::ivec2& chunkCoordinates, const std::string& worldSavePath);
		static void loadBlock(Vertex* vertexData, const glm::ivec3& vert1, const glm::ivec3& vert2, const glm::ivec3& vert3, const glm::ivec3& vert4, const TextureFormat& texture, CUBE_FACE face, bool colorFaceBasedOnBiome, glm::vec<4, uint8, glm::defaultp>& lightLevels, glm::vec<4, uint8, glm::defaultp>& skyLevels, const glm::ivec3& lightColor);
		static void calculateNextLightLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck);
		static void removeNextLightLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck, std::queue<glm::ivec3>& lightSources, bool ignoreThisSolidBlock);
		// TODO: Consider removing this duplication if it doesn't effect performance
		static void calculateNextSkyLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck);
		static void removeNextSkyLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck, std::queue<glm::ivec3>& lightSources, bool ignoreThisSolidBlock);
		static void calculateChunkLighting(Chunk* chunk, const glm::ivec2& chunkCoordinates);
		static void calculateChunkSkyBlocks(Chunk* chunk, const glm::ivec2& chunkCoordinates);

		void info()
		{
			g_logger_info("%d size of chunk", sizeof(Block) * World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);
			g_logger_info("Max %d size of vertex data", sizeof(Vertex) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth * 24);
		}

		const float maxBiomeHeight = 145.0f;
		const float minBiomeHeight = 55.0f;
		const int oceanLevel = 85;
		void generateTerrain(Chunk* chunk, const glm::ivec2& chunkCoordinates, float seed)
		{
			const int worldChunkX = chunkCoordinates.x * 16;
			const int worldChunkZ = chunkCoordinates.y * 16;

			g_memory_zeroMem(chunk->data, sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth);
			for (int x = 0; x < World::ChunkDepth; x++)
			{
				for (int z = 0; z < World::ChunkWidth; z++)
				{
					int16 maxHeight = TerrainGenerator::getHeight(x + worldChunkX, z + worldChunkZ, minBiomeHeight, maxBiomeHeight);
					int16 stoneHeight = (int16)(maxHeight - 3.0f);

					for (int y = 0; y < World::ChunkHeight; y++)
					{
						bool isCave = TerrainGenerator::getIsCave(x + worldChunkX, y, z + worldChunkZ, maxHeight);
						const int arrayExpansion = to1DArray(x, y, z);
						if (!isCave)
						{
							if (y == 0)
							{
								// Bedrock
								chunk->data[arrayExpansion].id = 7;
								// Set the first bit of compressed data to false, to let us know
								// this is not a transparent block
								chunk->data[arrayExpansion].setTransparent(false);
								chunk->data[arrayExpansion].setIsBlendable(false);
								chunk->data[arrayExpansion].setIsLightSource(false);
							}
							else if (y < stoneHeight)
							{
								// Stone
								chunk->data[arrayExpansion].id = 6;
								chunk->data[arrayExpansion].setTransparent(false);
								chunk->data[arrayExpansion].setIsBlendable(false);
								chunk->data[arrayExpansion].setIsLightSource(false);
							}
							else if (y < maxHeight)
							{
								// Dirt
								chunk->data[arrayExpansion].id = 4;
								chunk->data[arrayExpansion].setTransparent(false);
								chunk->data[arrayExpansion].setIsBlendable(false);
								chunk->data[arrayExpansion].setIsLightSource(false);
							}
							else if (y == maxHeight)
							{
								if (maxHeight < oceanLevel + 2)
								{
									// Sand
									chunk->data[arrayExpansion].id = 3;
									chunk->data[arrayExpansion].setTransparent(false);
									chunk->data[arrayExpansion].setIsBlendable(false);
									chunk->data[arrayExpansion].setIsLightSource(false);
								}
								else
								{
									// Grass
									chunk->data[arrayExpansion].id = 2;
									chunk->data[arrayExpansion].setTransparent(false);
									chunk->data[arrayExpansion].setIsBlendable(false);
									chunk->data[arrayExpansion].setIsLightSource(false);
								}
							}
							else if (y >= minBiomeHeight && y < oceanLevel)
							{
								// Water 
								chunk->data[arrayExpansion].id = 19;
								chunk->data[arrayExpansion].setTransparent(true);
								chunk->data[arrayExpansion].setIsBlendable(true);
								chunk->data[arrayExpansion].setIsLightSource(false);
							}
							else if (!chunk->data[arrayExpansion].id)
							{
								chunk->data[arrayExpansion].id = BlockMap::AIR_BLOCK.id;
								chunk->data[arrayExpansion].setTransparent(true);
								chunk->data[arrayExpansion].setIsBlendable(false);
								chunk->data[arrayExpansion].setIsLightSource(false);
							}
						}
						else
						{
							chunk->data[arrayExpansion].id = BlockMap::AIR_BLOCK.id;
							chunk->data[arrayExpansion].setTransparent(true);
							chunk->data[arrayExpansion].setIsBlendable(false);
							chunk->data[arrayExpansion].setIsLightSource(false);
							chunk->data[arrayExpansion].setLightColor(glm::ivec3(255, 255, 255));
						}
					}
				}
			}
		}

		void generateDecorations(const glm::ivec2& lastPlayerLoadPosChunkCoords, float seed)
		{
			for (int chunkZ = lastPlayerLoadPosChunkCoords.y - World::ChunkRadius; chunkZ <= lastPlayerLoadPosChunkCoords.y + World::ChunkRadius; chunkZ++)
			{
				for (int chunkX = lastPlayerLoadPosChunkCoords.x - World::ChunkRadius; chunkX <= lastPlayerLoadPosChunkCoords.x + World::ChunkRadius; chunkX++)
				{
					const int worldChunkX = chunkX * 16;
					const int worldChunkZ = chunkZ * 16;

					glm::ivec2 localChunkPos = glm::vec2(lastPlayerLoadPosChunkCoords.x - chunkX, lastPlayerLoadPosChunkCoords.y - chunkZ);
					bool inRangeOfPlayer =
						(localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) <=
						((World::ChunkRadius - 1) * (World::ChunkRadius - 1));
					if (!inRangeOfPlayer)
					{
						// Skip over all chunks in range radius - 1
						continue;
					}

					Chunk* chunk = ChunkManager::getChunk(glm::vec3(worldChunkX, 128.0f, worldChunkZ));
					if (!chunk)
					{
						// TODO: Is this a problem...? It should only effect chunks on the edge of the border
						// g_logger_error("Bad chunk when generating terrain. Skipping chunk.");
						continue;
					}

					if (!chunk->needsToGenerateDecorations)
					{
						continue;
					}
					chunk->needsToGenerateDecorations = false;

					for (int x = 0; x < World::ChunkDepth; x++)
					{
						for (int z = 0; z < World::ChunkWidth; z++)
						{
							// Generate some trees if needed
							int num = (rand() % 100);
							bool generateTree = num > 98;

							if (generateTree)
							{
								int16 y = TerrainGenerator::getHeight(x + worldChunkX, z + worldChunkZ, minBiomeHeight, maxBiomeHeight) + 1;

								if (y > oceanLevel + 2)
								{
									// Generate a tree
									int treeHeight = (rand() % 3) + 3;
									int leavesBottomY = glm::clamp(treeHeight - 3, 3, (int)World::ChunkHeight - 1);
									int leavesTopY = treeHeight + 1;
									if (generateTree && (y + 1 + leavesTopY < World::ChunkHeight))
									{
										for (int treeY = 0; treeY <= treeHeight; treeY++)
										{
											chunk->data[to1DArray(x, treeY + y, z)].id = 8;
											chunk->data[to1DArray(x, treeY + y, z)].setIsBlendable(false);
											chunk->data[to1DArray(x, treeY + y, z)].setTransparent(false);
											chunk->data[to1DArray(x, treeY + y, z)].setIsLightSource(false);
										}

										int ringLevel = 0;
										for (int leavesY = leavesBottomY + y; leavesY <= leavesTopY + y; leavesY++)
										{
											int leafRadius = leavesY == leavesTopY ? 2 : 1;
											for (int leavesX = x - leafRadius; leavesX <= x + leafRadius; leavesX++)
											{
												for (int leavesZ = z - leafRadius; leavesZ <= z + leafRadius; leavesZ++)
												{
													if (leavesX < World::ChunkDepth && leavesX >= 0 && leavesZ < World::ChunkWidth && leavesZ >= 0)
													{
														chunk->data[to1DArray(leavesX, leavesY, leavesZ)].id = 9;
														chunk->data[to1DArray(leavesX, leavesY, leavesZ)].setIsBlendable(false);
														chunk->data[to1DArray(leavesX, leavesY, leavesZ)].setTransparent(true);
														chunk->data[to1DArray(leavesX, leavesY, leavesZ)].setIsLightSource(false);
													}
													else if (leavesX < 0)
													{
														if (chunk->bottomNeighbor)
														{
															chunk->bottomNeighbor->data[to1DArray(World::ChunkDepth + leavesX, leavesY, leavesZ)].id = 9;
															chunk->data[to1DArray(World::ChunkDepth + leavesX, leavesY, leavesZ)].setIsBlendable(false);
															chunk->data[to1DArray(World::ChunkDepth + leavesX, leavesY, leavesZ)].setTransparent(true);
															chunk->data[to1DArray(World::ChunkDepth + leavesX, leavesY, leavesZ)].setIsLightSource(false);
														}
													}
													else if (leavesX >= World::ChunkDepth)
													{
														if (chunk->topNeighbor)
														{
															chunk->topNeighbor->data[to1DArray(leavesX - World::ChunkDepth, leavesY, leavesZ)].id = 9;
															chunk->data[to1DArray(leavesX - World::ChunkDepth, leavesY, leavesZ)].setIsBlendable(false);
															chunk->data[to1DArray(leavesX - World::ChunkDepth, leavesY, leavesZ)].setTransparent(true);
															chunk->data[to1DArray(leavesX - World::ChunkDepth, leavesY, leavesZ)].setIsLightSource(false);
														}
													}
													else if (leavesZ < 0)
													{
														if (chunk->leftNeighbor)
														{
															chunk->leftNeighbor->data[to1DArray(leavesX, leavesY, World::ChunkWidth + leavesZ)].id = 9;
															chunk->data[to1DArray(leavesX, leavesY, World::ChunkWidth + leavesZ)].setIsBlendable(false);
															chunk->data[to1DArray(leavesX, leavesY, World::ChunkWidth + leavesZ)].setTransparent(true);
															chunk->data[to1DArray(leavesX, leavesY, World::ChunkWidth + leavesZ)].setIsLightSource(false);
														}
													}
													else if (leavesZ >= World::ChunkWidth)
													{
														if (chunk->rightNeighbor)
														{
															chunk->rightNeighbor->data[to1DArray(leavesX, leavesY, leavesZ - World::ChunkWidth)].id = 9;
															chunk->data[to1DArray(leavesX, leavesY, leavesZ - World::ChunkWidth)].setIsBlendable(false);
															chunk->data[to1DArray(leavesX, leavesY, leavesZ - World::ChunkWidth)].setTransparent(true);
															chunk->data[to1DArray(leavesX, leavesY, leavesZ - World::ChunkWidth)].setIsLightSource(false);
														}
													}
												}
											}
											ringLevel++;
										}
									}
								}
							}
						}
					}
				}
			}
		}

		void calculateLighting(const glm::ivec2& lastPlayerLoadPosChunkCoords)
		{
			for (int i = 0; i < 2; i++)
			{
				for (int chunkZ = lastPlayerLoadPosChunkCoords.y - World::ChunkRadius; chunkZ <= lastPlayerLoadPosChunkCoords.y + World::ChunkRadius; chunkZ++)
				{
					for (int chunkX = lastPlayerLoadPosChunkCoords.x - World::ChunkRadius; chunkX <= lastPlayerLoadPosChunkCoords.x + World::ChunkRadius; chunkX++)
					{
						const int worldChunkX = chunkX * 16;
						const int worldChunkZ = chunkZ * 16;

						glm::ivec2 localChunkPos = glm::vec2(lastPlayerLoadPosChunkCoords.x - chunkX, lastPlayerLoadPosChunkCoords.y - chunkZ);
						bool inRangeOfPlayer =
							(localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) <=
							((World::ChunkRadius) * (World::ChunkRadius));
						if (!inRangeOfPlayer)
						{
							// Skip over all chunks in range radius - 1
							continue;
						}

						Chunk* chunk = ChunkManager::getChunk(glm::vec3(worldChunkX, 128.0f, worldChunkZ));
						if (!chunk)
						{
							// TODO: Is this a problem...? It should only effect chunks on the edge of the border
							// g_logger_error("Bad chunk when generating terrain. Skipping chunk.");
							continue;
						}

						if (!chunk->needsToCalculateLighting)
						{
							continue;
						}

						if (i == 0)
						{
							// First iteration calculate all sky light levels
							calculateChunkSkyBlocks(chunk, glm::ivec2(chunkX, chunkZ));
						}
						else if (i == 1)
						{
							// Second iteration calculate all sky "sources" and light sources
							calculateChunkLighting(chunk, glm::ivec2(chunkX, chunkZ));
							chunk->needsToCalculateLighting = false;
						}
					}
				}
			}
		}

		static void calculateChunkSkyBlocks(Chunk* chunk, const glm::ivec2& chunkCoordinates)
		{
			for (int x = 0; x < World::ChunkDepth; x++)
			{
				for (int z = 0; z < World::ChunkWidth; z++)
				{
					for (int y = World::ChunkHeight - 1; y >= 0; y--)
					{
						int arrayExpansion = to1DArray(x, y, z);
						if (!chunk->data[arrayExpansion].isTransparent())
						{
							// We're done propagating here
							break;
						}

						// Set the block to the max light level since this has to be a sky block
						chunk->data[arrayExpansion].setSkyLightLevel(31);
						chunk->data[arrayExpansion].setLightColor(glm::ivec3(255, 255, 255));
					}
				}
			}
		}

		static void calculateChunkLighting(Chunk* chunk, const glm::ivec2& chunkCoordinates)
		{
			// Propagate any sky blocks that are acting like "sources"
			std::queue<glm::ivec3> skyBlocksToUpdate = {};
			for (int y = World::ChunkHeight - 1; y >= 0; y--)
			{
				bool anyBlocksTransparent = false;
				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						int arrayExpansion = to1DArray(x, y, z);
						if (!chunk->data[arrayExpansion].isTransparent())
						{
							continue;
						}

						anyBlocksTransparent = true;
						if (chunk->data[arrayExpansion].calculatedSkyLightLevel() == 31)
						{
							// If any of the horizontal neighbors is transparent and not a sky block, add this block
							// as a source
							for (int i = 0; i < INormals3::XZCardinalDirections.size(); i++)
							{
								const glm::highp_ivec3 blockLocalPos = glm::highp_ivec3(x, y, z) + INormals3::CardinalDirections[i];
								Block block = getBlockInternal(chunk, blockLocalPos.x, blockLocalPos.y, blockLocalPos.z);
								if (block.isTransparent() && block.calculatedSkyLightLevel() != 31)
								{
									skyBlocksToUpdate.push({ x, y, z });
									break;
								}
							}
						}
					}
				}

				if (!anyBlocksTransparent)
				{
					break;
				}
			}
			robin_hood::unordered_flat_set<Chunk*> skyChunksToRetesselate = {};
			while (!skyBlocksToUpdate.empty())
			{
				calculateNextSkyLevel(chunk, chunkCoordinates, skyChunksToRetesselate, skyBlocksToUpdate);
			}

			// Then calculate all light sources
			std::queue<glm::ivec3> blocksToUpdate = {};
			for (int y = 0; y < World::ChunkHeight; y++)
			{
				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						int arrayExpansion = to1DArray(x, y, z);
						if (!chunk->data[arrayExpansion].isLightSource())
						{
							continue;
						}
						chunk->data[arrayExpansion].lightLevel = BlockMap::getBlock(chunk->data[arrayExpansion].id).lightLevel;
						blocksToUpdate.push({ x, y, z });
					}
				}
			}

			robin_hood::unordered_flat_set<Chunk*> chunksToRetesselate = {};
			while (!blocksToUpdate.empty())
			{
				calculateNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
			}
		}

		void calculateLightingUpdate(Chunk* chunk, const glm::ivec2& chunkCoordinates, const glm::vec3& blockPosition, bool removedLightSource, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate)
		{
			glm::ivec3 localPosition = glm::floor(blockPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			int localX = localPosition.x;
			int localY = localPosition.y;
			int localZ = localPosition.z;
			Block blockThatsUpdating = chunk->data[to1DArray(localX, localY, localZ)];
			if (!blockThatsUpdating.isTransparent() && !blockThatsUpdating.isLightSource() && !removedLightSource)
			{
				// Just placed a solid block
				std::queue<glm::ivec3> blocksToZero = {};
				std::queue<glm::ivec3> blocksToUpdate = {};
				blocksToZero.push({ localX, localY, localZ });
				bool ignoreThisSolidBlock = true;
				// Zero out
				while (!blocksToZero.empty())
				{
					removeNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToZero, blocksToUpdate, ignoreThisSolidBlock);
					ignoreThisSolidBlock = false;
				}

				// Flood fill from all the light sources that were found
				while (!blocksToUpdate.empty())
				{
					calculateNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
				}

				blocksToZero.push({ localX, localY, localZ });
				ignoreThisSolidBlock = true;
				// Zero out
				while (!blocksToZero.empty())
				{
					removeNextSkyLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToZero, blocksToUpdate, ignoreThisSolidBlock);
					ignoreThisSolidBlock = false;
				}

				// Flood fill from all the light sources that were found
				while (!blocksToUpdate.empty())
				{
					calculateNextSkyLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
				}
			}
			else if (removedLightSource)
			{
				// Just removed a light source
				std::queue<glm::ivec3> blocksToZero = {};
				std::queue<glm::ivec3> blocksToUpdate = {};
				blocksToZero.push({ localX, localY, localZ });
				// Zero out
				while (!blocksToZero.empty())
				{
					removeNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToZero, blocksToUpdate, false);
				}

				// Flood fill from all the light sources that were found
				while (!blocksToUpdate.empty())
				{
					calculateNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
				}
			}
			else if (blockThatsUpdating.isLightSource())
			{
				// Just added a light source
				std::queue<glm::ivec3> blocksToUpdate = {};
				blocksToUpdate.push({ localX, localY, localZ });
				int arrayExpansion = to1DArray(localX, localY, localZ);
				chunk->data[arrayExpansion].setLightLevel(BlockMap::getBlock(chunk->data[arrayExpansion].id).lightLevel);
				while (!blocksToUpdate.empty())
				{
					calculateNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
				}
			}
			else
			{
				// Just removed a block
				std::queue<glm::ivec3> blocksToUpdate = {};
				blocksToUpdate.push({ localX, localY, localZ });
				int arrayExpansion = to1DArray(localX, localY, localZ);

				// My light level is now the max of all my neighbors minus one
				int myLightLevel = 0;
				int mySkyLevel = 0;
				for (int i = 0; i < INormals3::CardinalDirections.size(); i++)
				{
					const glm::ivec3& normal = INormals3::CardinalDirections[i];
					Block block = getBlockInternal(chunk, localX + normal.x, localY + normal.y, localZ + normal.z);
					if ((block.calculatedLightLevel() - 1) > myLightLevel)
					{
						myLightLevel = block.calculatedLightLevel() - 1;
					}
					if ((block.calculatedSkyLightLevel() - 1) > mySkyLevel)
					{
						mySkyLevel = block.calculatedSkyLightLevel() - 1;
					}
					// If the block above me is a sky block, I am also a sky block
					if (block.calculatedSkyLightLevel() == 31 && normal.y == 1)
					{
						mySkyLevel = 31;
					}
				}
				chunk->data[arrayExpansion].setLightLevel(myLightLevel);
				while (!blocksToUpdate.empty())
				{
					calculateNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
				}

				chunk->data[arrayExpansion].setSkyLightLevel(mySkyLevel);
				blocksToUpdate.push({ localX, localY, localZ });
				// If I was a sky block, set all transparent blocks below me to sky blocks
				if (mySkyLevel == 31)
				{
					for (int y = localY; y >= 0; y--)
					{
						int otherBlockArrayExpansion = to1DArray(localX, y, localZ);
						Block otherBlock = chunk->data[otherBlockArrayExpansion];
						if (!otherBlock.isTransparent())
						{
							break;
						}

						chunk->data[arrayExpansion].setSkyLightLevel(31);
						blocksToUpdate.push({ localX, y, localZ });
					}
				}

				while (!blocksToUpdate.empty())
				{
					calculateNextSkyLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
				}
			}

			for (auto chunkA : chunksToRetesselate)
			{
				for (auto chunkB : chunksToRetesselate)
				{
					if (*chunkA != *chunkB)
					{
						if (chunkA->chunkCoords == chunkB->chunkCoords)
						{
							g_logger_warning("Duplicate, how did you get out of mother goose corner?!");
						}
					}
				}
			}
		}

		Block getLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, const Chunk* chunk)
		{
			return getBlockInternal(chunk, localPosition.x, localPosition.y, localPosition.z);
		}

		Block getBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, const Chunk* chunk)
		{
			glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			return getLocalBlock(localPosition, chunkCoordinates, chunk);
		}

		bool setLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, Chunk* chunk, Block newBlock)
		{
			return setBlockInternal(chunk, localPosition.x, localPosition.y, localPosition.z, newBlock);
		}

		bool setBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, Chunk* chunk, Block newBlock)
		{
			glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			return setLocalBlock(localPosition, chunkCoordinates, chunk, newBlock);
		}

		bool removeLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, Chunk* chunk)
		{
			return removeBlockInternal(chunk, localPosition.x, localPosition.y, localPosition.z);
		}

		bool removeBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, Chunk* chunk)
		{
			glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			return removeLocalBlock(localPosition, chunkCoordinates, chunk);
		}

		static SubChunk* getSubChunk(Pool<SubChunk>* subChunks, SubChunk* currentSubChunk, int currentLevel, const glm::ivec2& chunkCoordinates, bool isBlendableSubChunk)
		{
			bool needsNewChunk = currentSubChunk == nullptr
				|| currentSubChunk->subChunkLevel != currentLevel
				|| currentSubChunk->numVertsUsed + 6 >= World::MaxVertsPerSubChunk
				|| currentSubChunk->state != SubChunkState::TesselatingVertices;

			SubChunk* ret = currentSubChunk;
			if (needsNewChunk)
			{
				if (!subChunks->empty())
				{
					DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed + (World::MaxVertsPerSubChunk * sizeof(Vertex));

					ret = subChunks->getNewPool();
					ret->state = SubChunkState::TesselatingVertices;
					ret->subChunkLevel = currentLevel;
					ret->chunkCoordinates = chunkCoordinates;
					ret->isBlendable = isBlendableSubChunk;
				}
				else
				{
					g_logger_warning("Ran out of sub-chunk vertex room.");
					ret = nullptr;
				}
			}
			return ret;
		}

		void GetLightVerticesBySide(uint8_t side, glm::ivec3& v0, glm::ivec3& v1, glm::ivec3& v2, glm::ivec3& v3)
		{
			switch (side)
			{
			case 1: // Right
			{
				v0 += glm::ivec3(0, 0, 0);
				v1 += glm::ivec3(-1, 0, 0);
				v2 += glm::ivec3(-1, -1, 0);
				v3 += glm::ivec3(0, -1, 0);
				break;
			}

			case 0: // Left
			{
				v0 += glm::ivec3(0, 0, -1);
				v1 += glm::ivec3(-1, 0, -1);
				v2 += glm::ivec3(-1, -1, -1);
				v3 += glm::ivec3(0, -1, -1);
				break;
			}

			case 3:
			{ // Top
				v0 += glm::ivec3(0, 0, 0);
				v1 += glm::ivec3(-1, 0, 0);
				v2 += glm::ivec3(-1, 0, -1);
				v3 += glm::ivec3(0, 0, -1);
				break;
			}

			case 2:
			{ // Bottom
				v0 += glm::ivec3(0, -1, 0);
				v1 += glm::ivec3(-1, -1, 0);
				v2 += glm::ivec3(-1, -1, -1);
				v3 += glm::ivec3(0, -1, -1);
				break;
			}
			case 5: // Back
			{
				v0 += glm::ivec3(0, 0, 0);
				v1 += glm::ivec3(0, -1, 0);
				v2 += glm::ivec3(0, -1, -1);
				v3 += glm::ivec3(0, 0, -1);
				break;
			}

			case 4: // Front
			{
				v0 += glm::ivec3(-1, 0, 0);
				v1 += glm::ivec3(-1, -1, 0);
				v2 += glm::ivec3(-1, -1, -1);
				v3 += glm::ivec3(-1, 0, -1);
				break;
			}
			default:
				break;
			}
		}

		void generateRenderData(Pool<SubChunk>* subChunks, const Chunk* chunk, const glm::ivec2& chunkCoordinates, bool isRetesselation)
		{
			const int worldChunkX = chunkCoordinates.x * 16;
			const int worldChunkZ = chunkCoordinates.y * 16;

			SubChunk* solidSubChunk = nullptr;
			SubChunk* blendableSubChunk = nullptr;
			for (int y = 0; y < World::ChunkHeight; y++)
			{
				int currentLevel = y / 16;

				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						// 24 Vertices per cube
						const Block& block = getBlockInternal(chunk, x, y, z);
						int blockId = block.id;

						if (block.isNull() || block == BlockMap::AIR_BLOCK)
						{
							continue;
						}

						const BlockFormat& blockFormat = BlockMap::getBlock(blockId);
						bool currentBlockIsBlendable = blockFormat.isBlendable;
						bool currentBlockIsTransparent = blockFormat.isTransparent;
						bool currentBlockIsWater = blockId == 19;

						// TODO: SIMDify this section
						glm::ivec3 verts[8];
						verts[0] = glm::ivec3(
							x,
							y,
							z
						);
						verts[1] = verts[0] + INormals3::Right;
						verts[2] = verts[1] + INormals3::Front;
						verts[3] = verts[0] + INormals3::Front;

						verts[4] = verts[0] + INormals3::Up;
						verts[5] = verts[1] + INormals3::Up;
						verts[6] = verts[2] + INormals3::Up;
						verts[7] = verts[3] + INormals3::Up;

						// The order of coordinates is LEFT, RIGHT, BOTTOM, TOP, BACK, FRONT blocks to check
						int xCoords[6] = { x, x, x, x, x - 1, x + 1 };
						int yCoords[6] = { y, y, y - 1, y + 1, y, y };
						int zCoords[6] = { z - 1, z + 1, z, z, z, z };

						Block blocks[6];
						glm::ivec3 lightColors[6];
						const TextureFormat* textures[6] = {
							blockFormat.sideTexture,
							blockFormat.sideTexture,
							blockFormat.bottomTexture,
							blockFormat.topTexture,
							blockFormat.sideTexture,
							blockFormat.sideTexture
						};
						static const glm::ivec4 vertIndices[6] = {
							{0, 4, 7, 3}, // LEFT
							{2, 6, 5, 1}, // RIGHT
							{0, 3, 2, 1}, // BOTTOM
							{5, 6, 7, 4}, // TOP
							{0, 1, 5, 4}, // BACK
							{7, 6, 2, 3}  // FRONT
						};

						for (int i = 0; i < 6; i++)
						{
							blocks[i] = getBlockInternal(chunk, xCoords[i], yCoords[i], zCoords[i]);
							lightColors[i] = blocks[i].getCompressedLightColor();
						}

						SubChunk** currentSubChunkPtr = &solidSubChunk;
						if (currentBlockIsBlendable)
						{
							currentSubChunkPtr = &blendableSubChunk;
						}

						// Only add the faces that are not culled by other blocks
						for (int i = 0; i < 6; i++)
						{
							if (!blocks[i].isNull() && (blocks[i].isTransparent() && !currentBlockIsWater) || (blocks[i] == BlockMap::AIR_BLOCK && currentBlockIsWater))
							{
								// Smooth lighting
								glm::vec<4, uint8, glm::defaultp> smoothLightVertex[6] = {};
								glm::vec<4, uint8, glm::defaultp> smoothSkyLightVertex[6] = {};
								for (int v = 0; v < 4; v++)
								{
									glm::ivec3 v0 = verts[vertIndices[i][v]];
									glm::ivec3 v1 = verts[vertIndices[i][v]];
									glm::ivec3 v2 = verts[vertIndices[i][v]];
									glm::ivec3 v3 = verts[vertIndices[i][v]];
									GetLightVerticesBySide(i, v0, v1, v2, v3);

									const Block& v0b = getBlockInternal(chunk, v0.x, v0.y, v0.z);
									const Block& v1b = getBlockInternal(chunk, v1.x, v1.y, v1.z);
									const Block& v2b = getBlockInternal(chunk, v2.x, v2.y, v2.z);
									const Block& v3b = getBlockInternal(chunk, v3.x, v3.y, v3.z);

									uint8 count = 0;

									uint8 currentVertexLight = 0;
									uint8 currentVertexSkyLight = 0;

									if (v0b == BlockMap::NULL_BLOCK || v0b == BlockMap::AIR_BLOCK)
									{
										currentVertexLight += v0b.calculatedLightLevel();
										currentVertexSkyLight += v0b.calculatedSkyLightLevel();
										count++;
									}

									if (v1b == BlockMap::NULL_BLOCK || v1b == BlockMap::AIR_BLOCK)
									{
										currentVertexLight += v1b.calculatedLightLevel();
										currentVertexSkyLight += v1b.calculatedSkyLightLevel();
										count++;
									}

									if (v2b == BlockMap::NULL_BLOCK || v2b == BlockMap::AIR_BLOCK)
									{
										currentVertexLight += v2b.calculatedLightLevel();
										currentVertexSkyLight += v2b.calculatedSkyLightLevel();
										count++;
									}

									if (v3b == BlockMap::NULL_BLOCK || v3b == BlockMap::AIR_BLOCK)
									{
										currentVertexLight += v3b.calculatedLightLevel();
										currentVertexSkyLight += v3b.calculatedSkyLightLevel();
										count++;
									}

									if (count > 0)
									{
										currentVertexLight /= count;
										currentVertexSkyLight /= count;
									}

									smoothLightVertex[i][v] = currentVertexLight;
									smoothSkyLightVertex[i][v] = currentVertexSkyLight;
								}

								*currentSubChunkPtr = getSubChunk(subChunks, *currentSubChunkPtr, currentLevel, chunkCoordinates, currentBlockIsBlendable);
								SubChunk* currentSubChunk = *currentSubChunkPtr;
								if (!currentSubChunk)
								{
									// TODO: Handle running out of memory better than this
									break;
								}
								bool colorByBiome = i == (int)CUBE_FACE::TOP
									? blockFormat.colorTopByBiome
									: i == (int)CUBE_FACE::BOTTOM
									? blockFormat.colorBottomByBiome
									: blockFormat.colorSideByBiome;
								loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed,
									verts[vertIndices[i][0]],
									verts[vertIndices[i][1]],
									verts[vertIndices[i][2]],
									verts[vertIndices[i][3]],
									*textures[i],
									(CUBE_FACE)i,
									colorByBiome,
									smoothLightVertex[i],
									smoothSkyLightVertex[i],
									lightColors[i]);
								currentSubChunk->numVertsUsed += 6;
							}
						}
					}
				}
			}

			for (int i = 0; i < (int)(*subChunks).size(); i++)
			{
				if ((*subChunks)[i]->chunkCoordinates == chunkCoordinates)
				{
					if ((*subChunks)[i]->state == SubChunkState::RetesselateVertices)
					{
						SubChunk* subChunkToUnload = (*subChunks)[i];
						subChunkToUnload->state = SubChunkState::DoneRetesselating;
					}
					else if ((*subChunks)[i]->state == SubChunkState::TesselatingVertices)
					{
						SubChunk* subChunkToUnload = (*subChunks)[i];
						subChunkToUnload->state = SubChunkState::UploadVerticesToGpu;
					}
				}
			}
		}

		void serialize(const std::string& worldSavePath, const Chunk& chunk)
		{
			if ((Network::isNetworkEnabled() && Network::isLanServer()) || (!Network::isNetworkEnabled()))
			{
				std::string filepath = getFormattedFilepath(chunk.chunkCoords, worldSavePath);
				FILE* fp = fopen(filepath.c_str(), "wb");
				if (!fp)
				{
					g_logger_error("Failed to serialize chunk<%d, %d>");
					return;
				}
				RawMemory chunkData = chunk.serialize();
				fwrite(chunkData.data, chunkData.size, 1, fp);
				chunkData.free();
				fclose(fp);
			}
			else
			{
				g_logger_warning("Cannot serialize chunk over the network yet... I mean I can, I just don't feel like adding it in this part of the code.");
			}
		}

		void deserialize(Chunk& chunk, const std::string& worldSavePath)
		{
			if (!Network::isNetworkEnabled())
			{
				std::string filepath = getFormattedFilepath(chunk.chunkCoords, worldSavePath);
				FILE* fp = fopen(filepath.c_str(), "rb");
				if (!fp)
				{
					g_logger_error("Could not open file '%s'", filepath.c_str());
					return;
				}

				// Get file size
				fseek(fp, 0L, SEEK_END);
				size_t fileSize = ftell(fp);
				rewind(fp);

				// Read file into raw memory and deserialize
				RawMemory memory;
				memory.init(fileSize);
				fread(memory.data, memory.size, 1, fp);
				chunk.deserialize(memory);
				memory.free();

				// Close file
				fclose(fp);
			}
			else
			{
				g_logger_warning("Cannot deserialize chunk over the network yet...");
			}
		}

		bool exists(const std::string& worldSavePath, const glm::ivec2& chunkCoordinates)
		{
			std::string filepath = getFormattedFilepath(chunkCoordinates, worldSavePath);
			return File::isFile(filepath.c_str());
		}

		// =====================================================
		// Internal functions 
		// =====================================================
		static int to1DArray(int x, int y, int z)
		{
			return (x * World::ChunkDepth) + (y * World::ChunkHeight) + z;
		}

		static Block getBlockInternal(const Chunk* chunk, int x, int y, int z)
		{
			if (!chunk)
			{
				return BlockMap::NULL_BLOCK;
			}

			if (x >= World::ChunkDepth || x < 0 || z >= World::ChunkWidth || z < 0)
			{
				if (x >= World::ChunkDepth)
				{
					return getBlockInternal(chunk->topNeighbor, x - World::ChunkDepth, y, z);
				}
				else if (x < 0)
				{
					return getBlockInternal(chunk->bottomNeighbor, World::ChunkDepth + x, y, z);
				}

				if (z >= World::ChunkWidth)
				{
					return getBlockInternal(chunk->rightNeighbor, x, y, z - World::ChunkWidth);
				}
				else if (z < 0)
				{
					return getBlockInternal(chunk->leftNeighbor, x, y, World::ChunkWidth + z);
				}
			}
			else if (y >= World::ChunkHeight || y < 0)
			{
				return BlockMap::NULL_BLOCK;
			}

			int index = to1DArray(x, y, z);
			return chunk->data[index];
		}

		static bool setBlockInternal(Chunk* chunk, int x, int y, int z, Block newBlock)
		{
			if (!chunk)
			{
				return false;
			}

			if (x >= World::ChunkDepth || x < 0 || z >= World::ChunkWidth || z < 0)
			{
				if (x >= World::ChunkDepth)
				{
					return setBlockInternal(chunk->topNeighbor, x - World::ChunkDepth, y, z, newBlock);
				}
				else if (x < 0)
				{
					return setBlockInternal(chunk->bottomNeighbor, World::ChunkDepth + x, y, z, newBlock);
				}

				if (z >= World::ChunkWidth)
				{
					return setBlockInternal(chunk->rightNeighbor, x, y, z - World::ChunkWidth, newBlock);
				}
				else if (z < 0)
				{
					return setBlockInternal(chunk->leftNeighbor, x, y, World::ChunkWidth + z, newBlock);
				}
			}
			else if (y >= World::ChunkHeight || y < 0)
			{
				return false;
			}

			int index = to1DArray(x, y, z);
			BlockFormat blockFormat = BlockMap::getBlock(newBlock.id);
			chunk->data[index].id = newBlock.id;
			chunk->data[index].setTransparent(blockFormat.isTransparent);
			chunk->data[index].setIsLightSource(blockFormat.isLightSource);

			return true;
		}

		static bool removeBlockInternal(Chunk* chunk, int x, int y, int z)
		{
			if (!chunk)
			{
				return false;
			}

			if (x >= World::ChunkDepth || x < 0 || z >= World::ChunkWidth || z < 0)
			{
				if (x >= World::ChunkDepth)
				{
					return removeBlockInternal(chunk->topNeighbor, x - World::ChunkDepth, y, z);
				}
				else if (x < 0)
				{
					return removeBlockInternal(chunk->bottomNeighbor, World::ChunkDepth + x, y, z);
				}

				if (z >= World::ChunkWidth)
				{
					return removeBlockInternal(chunk->rightNeighbor, x, y, z - World::ChunkWidth);
				}
				else if (z < 0)
				{
					return removeBlockInternal(chunk->leftNeighbor, x, y, World::ChunkWidth + z);
				}
			}
			else if (y >= World::ChunkHeight || y < 0)
			{
				return false;
			}

			int index = to1DArray(x, y, z);
			chunk->data[index].id = BlockMap::AIR_BLOCK.id;
			chunk->data[index].setLightColor(glm::ivec3(255, 255, 255));
			chunk->data[index].setTransparent(true);
			chunk->data[index].setIsLightSource(false);

			return true;
		}

		static bool checkPositionInBounds(Chunk** currentChunk, int* x, int y, int* z)
		{
			if (y < 0 || y >= World::ChunkHeight)
			{
				return false;
			}

			// Do a while loop, because the block could theoretically be like -32 which would be two chunks out
			while (*x < 0)
			{
				*currentChunk = (*currentChunk)->bottomNeighbor;
				*x = World::ChunkDepth + *x;
				if (!(*currentChunk))
				{
					return false;
				}
			}
			while (*z < 0)
			{
				*currentChunk = (*currentChunk)->leftNeighbor;
				*z = World::ChunkWidth + *z;
				if (!(*currentChunk))
				{
					return false;
				}
			}
			while (*x >= World::ChunkDepth)
			{
				*currentChunk = (*currentChunk)->topNeighbor;
				*x = *x - World::ChunkDepth;
				if (!(*currentChunk))
				{
					return false;
				}
			}
			while (*z >= World::ChunkWidth)
			{
				*currentChunk = (*currentChunk)->rightNeighbor;
				*z = *z - World::ChunkWidth;
				if (!(*currentChunk))
				{
					return false;
				}
			}

			return true;
		}

		static void calculateNextLightLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck)
		{
			glm::ivec3 blockToUpdate = blocksToCheck.front();
			blocksToCheck.pop();

			if (!originalChunk)
			{
				g_logger_warning("Encountered weird null chunk while updating block lighting.");
				return;
			}

			int blockToUpdateX = blockToUpdate.x;
			int blockToUpdateY = blockToUpdate.y;
			int blockToUpdateZ = blockToUpdate.z;
			Chunk* blockToUpdateChunk = originalChunk;
			if (blockToUpdateX >= World::ChunkDepth || blockToUpdateX < 0 || blockToUpdateZ >= World::ChunkWidth || blockToUpdateZ < 0)
			{
				if (!checkPositionInBounds(&blockToUpdateChunk, &blockToUpdateX, blockToUpdateY, &blockToUpdateZ))
				{
					g_logger_warning("Position totally out of bounds...");
					return;
				}
				chunksToRetesselate.insert(blockToUpdateChunk);
			}

			int arrayExpansion = to1DArray(blockToUpdateX, blockToUpdateY, blockToUpdateZ);
			if (!blockToUpdateChunk->data[arrayExpansion].isTransparent() &&
				!blockToUpdateChunk->data[arrayExpansion].isLightSource())
			{
				return;
			}

			int myLightLevel = blockToUpdateChunk->data[arrayExpansion].calculatedLightLevel();
			if (myLightLevel > 0)
			{
				for (int i = 0; i < INormals3::CardinalDirections.size(); i++)
				{
					const glm::ivec3& iNormal = INormals3::CardinalDirections[i];
					const glm::ivec3 pos = glm::ivec3(blockToUpdateX + iNormal.x, blockToUpdateY + iNormal.y, blockToUpdateZ + iNormal.z);
					Block neighbor = getBlockInternal(blockToUpdateChunk, pos.x, pos.y, pos.z);
					int neighborLight = neighbor.calculatedLightLevel();
					if (neighborLight <= myLightLevel - 2 && neighbor.isTransparent())
					{
						Chunk* neighborChunk = blockToUpdateChunk;
						int neighborLocalX = pos.x;
						int neighborLocalZ = pos.z;
						if (checkPositionInBounds(&neighborChunk, &neighborLocalX, pos.y, &neighborLocalZ))
						{
							neighborChunk->data[to1DArray(neighborLocalX, pos.y, neighborLocalZ)].setLightLevel(myLightLevel - 1);
							neighborChunk->data[to1DArray(neighborLocalX, pos.y, neighborLocalZ)].setLightColor(glm::ivec3(255, 255, 255));
							blocksToCheck.push(glm::ivec3(blockToUpdate.x + iNormal.x, blockToUpdate.y + iNormal.y, blockToUpdate.z + iNormal.z));
							chunksToRetesselate.insert(neighborChunk);
						}
					}
				}
			}
		}

		static void removeNextLightLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck, std::queue<glm::ivec3>& lightSources, bool ignoreThisSolidBlock)
		{
			glm::ivec3 blockToUpdate = blocksToCheck.front();
			blocksToCheck.pop();

			if (!originalChunk)
			{
				g_logger_warning("Encountered weird null chunk while updating block lighting.");
				return;
			}

			int blockToUpdateX = blockToUpdate.x;
			int blockToUpdateY = blockToUpdate.y;
			int blockToUpdateZ = blockToUpdate.z;
			Chunk* blockToUpdateChunk = originalChunk;
			if (blockToUpdateX >= World::ChunkDepth || blockToUpdateX < 0 || blockToUpdateZ >= World::ChunkWidth || blockToUpdateZ < 0)
			{
				if (!checkPositionInBounds(&blockToUpdateChunk, &blockToUpdateX, blockToUpdateY, &blockToUpdateZ))
				{
					g_logger_warning("Position totally out of bounds...");
					return;
				}
				chunksToRetesselate.insert(blockToUpdateChunk);
			}

			int arrayExpansion = to1DArray(blockToUpdateX, blockToUpdateY, blockToUpdateZ);
			if (!ignoreThisSolidBlock &&
				!blockToUpdateChunk->data[arrayExpansion].isTransparent() &&
				!blockToUpdateChunk->data[arrayExpansion].isLightSource())
			{
				return;
			}

			int myOldLightLevel = blockToUpdateChunk->data[arrayExpansion].calculatedLightLevel();
			blockToUpdateChunk->data[arrayExpansion].setLightLevel(0);
			for (int i = 0; i < INormals3::CardinalDirections.size(); i++)
			{
				const glm::ivec3& iNormal = INormals3::CardinalDirections[i];
				const glm::ivec3 pos = glm::ivec3(blockToUpdateX + iNormal.x, blockToUpdateY + iNormal.y, blockToUpdateZ + iNormal.z);
				Block neighbor = getBlockInternal(blockToUpdateChunk, pos.x, pos.y, pos.z);
				int neighborLight = neighbor.calculatedLightLevel();
				if (neighborLight != 0 && neighborLight < myOldLightLevel && neighbor.isTransparent())
				{
					Chunk* neighborChunk = blockToUpdateChunk;
					int neighborLocalX = pos.x;
					int neighborLocalZ = pos.z;
					if (checkPositionInBounds(&neighborChunk, &neighborLocalX, pos.y, &neighborLocalZ))
					{
						blocksToCheck.push(glm::ivec3(blockToUpdate.x + iNormal.x, blockToUpdate.y + iNormal.y, blockToUpdate.z + iNormal.z));
						chunksToRetesselate.insert(neighborChunk);
					}
				}
				else if (neighborLight > myOldLightLevel)
				{
					Chunk* neighborChunk = blockToUpdateChunk;
					int neighborLocalX = pos.x;
					int neighborLocalZ = pos.z;
					if (checkPositionInBounds(&neighborChunk, &neighborLocalX, pos.y, &neighborLocalZ))
					{
						lightSources.push(glm::ivec3(blockToUpdate.x + iNormal.x, blockToUpdate.y + iNormal.y, blockToUpdate.z + iNormal.z));
						chunksToRetesselate.insert(neighborChunk);
					}
				}
			}
		}

		// TODO: Think about removing this duplication if it doesn't effect performance
		static void calculateNextSkyLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck)
		{
			glm::ivec3 blockToUpdate = blocksToCheck.front();
			blocksToCheck.pop();

			if (!originalChunk)
			{
				g_logger_warning("Encountered weird null chunk while updating block lighting.");
				return;
			}

			int blockToUpdateX = blockToUpdate.x;
			int blockToUpdateY = blockToUpdate.y;
			int blockToUpdateZ = blockToUpdate.z;
			Chunk* blockToUpdateChunk = originalChunk;
			if (blockToUpdateX >= World::ChunkDepth || blockToUpdateX < 0 || blockToUpdateZ >= World::ChunkWidth || blockToUpdateZ < 0)
			{
				if (!checkPositionInBounds(&blockToUpdateChunk, &blockToUpdateX, blockToUpdateY, &blockToUpdateZ))
				{
					return;
				}
				chunksToRetesselate.insert(blockToUpdateChunk);
			}

			int arrayExpansion = to1DArray(blockToUpdateX, blockToUpdateY, blockToUpdateZ);
			if (!blockToUpdateChunk->data[arrayExpansion].isTransparent())
			{
				return;
			}

			int myLightLevel = blockToUpdateChunk->data[arrayExpansion].calculatedSkyLightLevel();
			if (myLightLevel > 0)
			{
				for (int i = 0; i < INormals3::CardinalDirections.size(); i++)
				{
					const glm::ivec3& iNormal = INormals3::CardinalDirections[i];
					const glm::ivec3 pos = glm::ivec3(blockToUpdateX + iNormal.x, blockToUpdateY + iNormal.y, blockToUpdateZ + iNormal.z);
					Block neighbor = getBlockInternal(blockToUpdateChunk, pos.x, pos.y, pos.z);
					int neighborLight = neighbor.calculatedSkyLightLevel();
					if (neighborLight <= myLightLevel - 2 && neighbor.isTransparent())
					{
						Chunk* neighborChunk = blockToUpdateChunk;
						int neighborLocalX = pos.x;
						int neighborLocalZ = pos.z;
						if (checkPositionInBounds(&neighborChunk, &neighborLocalX, pos.y, &neighborLocalZ))
						{
							neighborChunk->data[to1DArray(neighborLocalX, pos.y, neighborLocalZ)].setSkyLightLevel(myLightLevel - 1);
							neighborChunk->data[to1DArray(neighborLocalX, pos.y, neighborLocalZ)].setLightColor(glm::ivec3(255, 255, 255));
							blocksToCheck.push(glm::ivec3(blockToUpdate.x + iNormal.x, blockToUpdate.y + iNormal.y, blockToUpdate.z + iNormal.z));
							//g_logger_assert(iNormal.y != 1, "Sky sources should never propagate up once we get inside of here.");
							chunksToRetesselate.insert(neighborChunk);
						}
					}
				}
			}
		}

		static void removeNextSkyLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck, std::queue<glm::ivec3>& lightSources, bool ignoreThisSolidBlock)
		{
			glm::ivec3 blockToUpdate = blocksToCheck.front();
			blocksToCheck.pop();

			if (!originalChunk)
			{
				g_logger_warning("Encountered weird null chunk while updating block lighting.");
				return;
			}

			int blockToUpdateX = blockToUpdate.x;
			int blockToUpdateY = blockToUpdate.y;
			int blockToUpdateZ = blockToUpdate.z;
			Chunk* blockToUpdateChunk = originalChunk;
			if (blockToUpdateX >= World::ChunkDepth || blockToUpdateX < 0 || blockToUpdateZ >= World::ChunkWidth || blockToUpdateZ < 0)
			{
				if (!checkPositionInBounds(&blockToUpdateChunk, &blockToUpdateX, blockToUpdateY, &blockToUpdateZ))
				{
					return;
				}
				chunksToRetesselate.insert(blockToUpdateChunk);
			}

			int arrayExpansion = to1DArray(blockToUpdateX, blockToUpdateY, blockToUpdateZ);
			if (!ignoreThisSolidBlock &&
				!blockToUpdateChunk->data[arrayExpansion].isTransparent())
			{
				return;
			}

			int myOldLightLevel = blockToUpdateChunk->data[arrayExpansion].calculatedSkyLightLevel();
			blockToUpdateChunk->data[arrayExpansion].setSkyLightLevel(0);
			for (int i = 0; i < INormals3::CardinalDirections.size(); i++)
			{
				const glm::ivec3& iNormal = INormals3::CardinalDirections[i];
				const glm::ivec3 pos = glm::ivec3(blockToUpdateX + iNormal.x, blockToUpdateY + iNormal.y, blockToUpdateZ + iNormal.z);
				Block neighbor = getBlockInternal(blockToUpdateChunk, pos.x, pos.y, pos.z);
				int neighborLight = neighbor.calculatedSkyLightLevel();
				bool neighborLightEffectedByMe = (neighborLight < myOldLightLevel) || (myOldLightLevel == 31 && iNormal.y == -1);
				if (neighborLight != 0 && neighborLightEffectedByMe && neighbor.isTransparent())
				{
					Chunk* neighborChunk = blockToUpdateChunk;
					int neighborLocalX = pos.x;
					int neighborLocalZ = pos.z;
					if (checkPositionInBounds(&neighborChunk, &neighborLocalX, pos.y, &neighborLocalZ))
					{
						blocksToCheck.push(glm::ivec3(blockToUpdate.x + iNormal.x, blockToUpdate.y + iNormal.y, blockToUpdate.z + iNormal.z));
					}
					chunksToRetesselate.insert(neighborChunk);
				}
				else if (neighborLight > myOldLightLevel)
				{
					Chunk* neighborChunk = blockToUpdateChunk;
					int neighborLocalX = pos.x;
					int neighborLocalZ = pos.z;
					if (checkPositionInBounds(&neighborChunk, &neighborLocalX, pos.y, &neighborLocalZ))
					{
						lightSources.push(glm::ivec3(blockToUpdate.x + iNormal.x, blockToUpdate.y + iNormal.y, blockToUpdate.z + iNormal.z));
					}
					chunksToRetesselate.insert(neighborChunk);
				}
			}
		}

		static std::string getFormattedFilepath(const glm::ivec2& chunkCoordinates, const std::string& worldSavePath)
		{
			return worldSavePath + "/" + std::to_string(chunkCoordinates.x) + "_" + std::to_string(chunkCoordinates.y) + ".bin";
		}

		static int toCompressedVec3(int x, int y, int z)
		{
			return (x * BASE_17_DEPTH) + (y * BASE_17_HEIGHT) + z;
		}

		static glm::ivec3 toCoordinates(int index)
		{
			const int z = index % BASE_17_WIDTH;
			const int x = (index % BASE_17_HEIGHT) / BASE_17_DEPTH;
			const int y = (index - (x * BASE_17_DEPTH) - z) / BASE_17_HEIGHT;
			return {
				x, y, z
			};
		}

		static Vertex compress(
			const glm::ivec3& vertex,
			const TextureFormat& texture,
			CUBE_FACE face,
			UV_INDEX uvIndex,
			bool colorVertexBasedOnBiome,
			int lightLevel,
			const glm::ivec3& lightColor,
			int skyLightLevel)
		{
			// Bits  0-16 position index
			// Bits 17-28 texId
			// Bits 29-31 normalDir face value
			uint32 data1 = 0;

			int positionIndex = toCompressedVec3(vertex.x, vertex.y, vertex.z);
			data1 |= ((positionIndex << 0) & POSITION_INDEX_BITMASK);
			data1 |= ((texture.id << 17) & TEX_ID_BITMASK);
			data1 |= ((uint32)face << 29) & FACE_BITMASK;

			uint32 data2 = 0;

			// Bits  0- 1 UV Index -- this tells us which corner to use for the texture coords
			// Bit      2 Color the block based on biome
			// Bits  4- 8 Light level
			// Bits  9-17 Light color
			// Bits 17-22 Sky Light Level
			data2 |= (((uint32)uvIndex << 0) & UV_INDEX_BITMASK);
			data2 |= (((uint32)(colorVertexBasedOnBiome ? 1 : 0) << 2) & COLOR_BLOCK_BIOME_BITMASK);
			data2 |= (((uint32)(lightLevel << 3) & LIGHT_LEVEL_BITMASK));
			data2 |= (((uint32)(lightColor.r << 8) & LIGHT_COLOR_BITMASK_R));
			data2 |= (((uint32)(lightColor.g << 11) & LIGHT_COLOR_BITMASK_G));
			data2 |= (((uint32)(lightColor.b << 14) & LIGHT_COLOR_BITMASK_B));
			data2 |= (((uint32)(skyLightLevel << 17) & SKY_LIGHT_LEVEL_BITMASK));

			return {
				data1,
				data2
			};
		}

		static void loadBlock(
			Vertex* vertexData,
			const glm::ivec3& vert1,
			const glm::ivec3& vert2,
			const glm::ivec3& vert3,
			const glm::ivec3& vert4,
			const TextureFormat& texture,
			CUBE_FACE face,
			bool colorFaceBasedOnBiome,
			glm::vec<4, uint8, glm::defaultp>& lightLevels,
			glm::vec<4, uint8, glm::defaultp>& skyLightLevels,
			const glm::ivec3& lightColor)
		{
			UV_INDEX uv0 = UV_INDEX::BOTTOM_RIGHT;
			UV_INDEX uv1 = UV_INDEX::TOP_RIGHT;
			UV_INDEX uv2 = UV_INDEX::TOP_LEFT;

			UV_INDEX uv3 = UV_INDEX::BOTTOM_RIGHT;
			UV_INDEX uv4 = UV_INDEX::TOP_LEFT;
			UV_INDEX uv5 = UV_INDEX::BOTTOM_LEFT;

			switch (face)
			{
			case CUBE_FACE::BACK:
				uv0 = (UV_INDEX)(((int)uv0 + 2) % (int)UV_INDEX::SIZE);
				uv1 = (UV_INDEX)(((int)uv1 + 2) % (int)UV_INDEX::SIZE);
				uv2 = (UV_INDEX)(((int)uv2 + 2) % (int)UV_INDEX::SIZE);
				uv3 = (UV_INDEX)(((int)uv3 + 2) % (int)UV_INDEX::SIZE);
				uv4 = (UV_INDEX)(((int)uv4 + 2) % (int)UV_INDEX::SIZE);
				uv5 = (UV_INDEX)(((int)uv5 + 2) % (int)UV_INDEX::SIZE);
				break;
			case CUBE_FACE::RIGHT:
				uv0 = (UV_INDEX)(((int)uv0 + 3) % (int)UV_INDEX::SIZE);
				uv1 = (UV_INDEX)(((int)uv1 + 3) % (int)UV_INDEX::SIZE);
				uv2 = (UV_INDEX)(((int)uv2 + 3) % (int)UV_INDEX::SIZE);
				uv3 = (UV_INDEX)(((int)uv3 + 3) % (int)UV_INDEX::SIZE);
				uv4 = (UV_INDEX)(((int)uv4 + 3) % (int)UV_INDEX::SIZE);
				uv5 = (UV_INDEX)(((int)uv5 + 3) % (int)UV_INDEX::SIZE);
				break;
			case CUBE_FACE::LEFT:
				uv0 = (UV_INDEX)(((int)uv0 + 3) % (int)UV_INDEX::SIZE);
				uv1 = (UV_INDEX)(((int)uv1 + 3) % (int)UV_INDEX::SIZE);
				uv2 = (UV_INDEX)(((int)uv2 + 3) % (int)UV_INDEX::SIZE);
				uv3 = (UV_INDEX)(((int)uv3 + 3) % (int)UV_INDEX::SIZE);
				uv4 = (UV_INDEX)(((int)uv4 + 3) % (int)UV_INDEX::SIZE);
				uv5 = (UV_INDEX)(((int)uv5 + 3) % (int)UV_INDEX::SIZE);
				break;
			}

			vertexData[0] = compress(vert1, texture, face, uv0, colorFaceBasedOnBiome, lightLevels[0], lightColor, skyLightLevels[0]);
			vertexData[1] = compress(vert2, texture, face, uv1, colorFaceBasedOnBiome, lightLevels[1], lightColor, skyLightLevels[1]);
			vertexData[2] = compress(vert3, texture, face, uv2, colorFaceBasedOnBiome, lightLevels[2], lightColor, skyLightLevels[2]);

			vertexData[3] = compress(vert1, texture, face, uv3, colorFaceBasedOnBiome, lightLevels[0], lightColor, skyLightLevels[0]);
			vertexData[4] = compress(vert3, texture, face, uv4, colorFaceBasedOnBiome, lightLevels[2], lightColor, skyLightLevels[2]);
			vertexData[5] = compress(vert4, texture, face, uv5, colorFaceBasedOnBiome, lightLevels[3], lightColor, skyLightLevels[3]);
		}
	}
}