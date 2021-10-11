#include "world/Chunk.h"
#include "world/World.h"
#include "utils/YamlExtended.h"
#include "world/BlockMap.h"
#include "utils/CMath.h"
#include "core/File.h"

namespace Minecraft
{
	static const int POSITION_INDEX_BITMASK = 0x1FFFF;
	static const int TEX_ID_BITMASK = 0x1FFE0000;
	static const int FACE_BITMASK = 0xE0000000;
	static const int UV_INDEX_BITMASK = 0x3;

	enum class CUBE_FACE : uint32
	{
		LEFT = 0,
		RIGHT = 1,
		BOTTOM = 2,
		TOP = 3,
		BACK = 4,
		FRONT = 5
	};

	enum class UV_INDEX : uint32
	{
		TOP_RIGHT = 0,
		TOP_LEFT = 1,
		BOTTOM_LEFT = 2,
		BOTTOM_RIGHT = 3
	};

	static int to1DArray(int x, int y, int z)
	{
		return (x * World::ChunkDepth) + (y * World::ChunkHeight) + z;
	}

	static const Block& getBlockInternal(Block* data, int x, int y, int z, const Chunk& chunk)
	{
		int index = to1DArray(x, y, z);
		return x >= 16 || x < 0 || z >= 16 || z < 0
			? World::getBlock(glm::vec3(chunk.chunkCoordinates.x * 16.0f + x, y, chunk.chunkCoordinates.y * 16.0f + z))
			: y >= 256 || y < 0
			? BlockMap::NULL_BLOCK
			: data[index];
	}

	static void loadBlock(
		Vertex* vertexData,
		const glm::ivec3& vert1,
		const glm::ivec3& vert2,
		const glm::ivec3& vert3,
		const glm::ivec3& vert4,
		const TextureFormat& texture,
		CUBE_FACE face);


	bool operator==(const Block& a, const Block& b)
	{
		return a.id == b.id;
	}

	void Chunk::info()
	{
		g_logger_info("%d size of chunk", sizeof(Block) * World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);
		g_logger_info("Max %d size of vertex data", sizeof(Vertex) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth * 24);
	}

	void Chunk::generate(Block* newChunkData, int chunkX, int chunkZ, int32 seed)
	{
		const int worldChunkX = chunkX * 16;
		const int worldChunkZ = chunkZ * 16;

		chunkCoordinates = { chunkX, chunkZ };

		chunkData = newChunkData;
		g_memory_zeroMem(chunkData, sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth);
		const SimplexNoise generator = SimplexNoise();
		for (int y = 0; y < World::ChunkHeight; y++)
		{
			for (int x = 0; x < World::ChunkDepth; x++)
			{
				for (int z = 0; z < World::ChunkWidth; z++)
				{
					// 24 Vertices per cube
					const float incrementSize = 1000.0f;
					const int arrayExpansion = to1DArray(x, y, z);
					float maxHeightFloat =
						CMath::mapRange(
							generator.fractal(
								7,
								(x + worldChunkX + seed) / incrementSize,
								(z + worldChunkZ + seed) / incrementSize
							),
							-1.0f,
							1.0f,
							0.0f,
							1.0f
						) * 255.0f;
					int16 maxHeight = (int16)maxHeightFloat;

					float stoneHeightFloat =
						CMath::mapRange(
							generator.fractal(
								7,
								(x + worldChunkX + seed) / incrementSize,
								(z + worldChunkZ + seed) / incrementSize
							),
							-1.0f,
							1.0f,
							0.0f,
							1.0f
						) * 255.0f;
					int16 stoneHeight = (int16)((stoneHeightFloat * maxHeightFloat) / 127.0f);

					if (y == 0)
					{
						// Bedrock
						chunkData[arrayExpansion].id = 7;
					}
					else if (y < stoneHeight)
					{
						// Stone
						chunkData[arrayExpansion].id = 6;
					}
					else if (y < maxHeight)
					{
						// Dirt
						chunkData[arrayExpansion].id = 4;
					}
					else if (y == maxHeight)
					{
						// Green Concrete 
						chunkData[arrayExpansion].id = 5;
					}
				}
			}
		}
	}

	Block Chunk::getBlock(glm::vec3 worldPosition)
	{
		glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
		return getLocalBlock(localPosition);
	}

	Block Chunk::getLocalBlock(glm::ivec3 localPosition)
	{
		return getBlockInternal(chunkData, localPosition.x, localPosition.y, localPosition.z, *this);
	}

	void Chunk::generateRenderData(SubChunk* subChunks)
	{
		const int worldChunkX = chunkCoordinates.x * 16;
		const int worldChunkZ = chunkCoordinates.y * 16;

		SubChunk* currentSubChunk = subChunks;
		currentSubChunk->numVertsUsed = 0;
		for (int y = 0; y < World::ChunkHeight; y++)
		{
			if (y % 16 == 0)
			{
				uint32 subChunkIndex = y / 16;
				g_logger_assert(subChunkIndex >= 0 && subChunkIndex < 16, "Invalid sub-chunk index.");
				currentSubChunk->uploadVertsToGpu = true;
				currentSubChunk = subChunks + subChunkIndex;
				currentSubChunk->numVertsUsed = 0;
			}

			for (int x = 0; x < World::ChunkDepth; x++)
			{
				for (int z = 0; z < World::ChunkWidth; z++)
				{
					// 24 Vertices per cube
					const Block& block = getBlockInternal(chunkData, x, y, z, *this);
					int blockId = block.id;

					if (block == BlockMap::NULL_BLOCK)
					{
						continue;
					}

					const BlockFormat& blockFormat = BlockMap::getBlock(blockId);
					const TextureFormat& side = BlockMap::getTextureFormat(blockFormat.sideTexture);
					const TextureFormat& top = BlockMap::getTextureFormat(blockFormat.topTexture);
					const TextureFormat& bottom = BlockMap::getTextureFormat(blockFormat.bottomTexture);

					glm::ivec3 verts[8];
					verts[0] = glm::ivec3(
						x,
						y,
						z
					);
					verts[1] = verts[0] + glm::ivec3(0, 0, 1);
					verts[2] = verts[1] + glm::ivec3(1, 0, 0);
					verts[3] = verts[0] + glm::ivec3(1, 0, 0);

					verts[4] = verts[0] + glm::ivec3(0, 1, 0);
					verts[5] = verts[1] + glm::ivec3(0, 1, 0);
					verts[6] = verts[2] + glm::ivec3(0, 1, 0);
					verts[7] = verts[3] + glm::ivec3(0, 1, 0);

					// Top Face
					const int topBlockId = getBlockInternal(chunkData, x, y + 1, z, *this).id;
					const BlockFormat& topBlock = BlockMap::getBlock(topBlockId);
					if (!topBlockId || topBlock.isTransparent)
					{
						g_logger_assert(currentSubChunk->numVertsUsed + 6 <= World::MaxVertsPerSubChunk, "Ran out of room in sub-chunk! %d", currentSubChunk->numVertsUsed.load());
						loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[5], verts[6], verts[7], verts[4],
							top, CUBE_FACE::TOP);
						currentSubChunk->numVertsUsed += 6;
					}

					// Bottom Face
					const int bottomBlockId = getBlockInternal(chunkData, x, y - 1, z, *this).id;
					const BlockFormat& bottomBlock = BlockMap::getBlock(bottomBlockId);
					if (!bottomBlockId || bottomBlock.isTransparent)
					{
						g_logger_assert(currentSubChunk->numVertsUsed + 6 <= World::MaxVertsPerSubChunk, "Ran out of room in sub-chunk! %d", currentSubChunk->numVertsUsed.load());
						loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[0], verts[3], verts[2], verts[1],
							bottom, CUBE_FACE::BOTTOM);
						currentSubChunk->numVertsUsed += 6;
					}

					// Right Face
					const int rightBlockId = getBlockInternal(chunkData, x, y, z + 1, *this).id;
					const BlockFormat& rightBlock = BlockMap::getBlock(rightBlockId);
					if (!rightBlockId || rightBlock.isTransparent)
					{
						g_logger_assert(currentSubChunk->numVertsUsed + 6 <= World::MaxVertsPerSubChunk, "Ran out of room in sub-chunk! %d", currentSubChunk->numVertsUsed.load());
						loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[2], verts[6], verts[5], verts[1],
							side, CUBE_FACE::RIGHT);
						currentSubChunk->numVertsUsed += 6;
					}

					// Left Face
					const int leftBlockId = getBlockInternal(chunkData, x, y, z - 1, *this).id;
					const BlockFormat& leftBlock = BlockMap::getBlock(leftBlockId);
					if (!leftBlockId || leftBlock.isTransparent)
					{
						g_logger_assert(currentSubChunk->numVertsUsed + 6 <= World::MaxVertsPerSubChunk, "Ran out of room in sub-chunk! %d", currentSubChunk->numVertsUsed.load());
						loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[0], verts[4], verts[7], verts[3],
							side, CUBE_FACE::LEFT);
						currentSubChunk->numVertsUsed += 6;
					}

					// Forward Face
					const int forwardBlockId = getBlockInternal(chunkData, x + 1, y, z, *this).id;
					const BlockFormat& forwardBlock = BlockMap::getBlock(forwardBlockId);
					if (!forwardBlockId || forwardBlock.isTransparent)
					{
						g_logger_assert(currentSubChunk->numVertsUsed + 6 <= World::MaxVertsPerSubChunk, "Ran out of room in sub-chunk! %d", currentSubChunk->numVertsUsed.load());
						loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[7], verts[6], verts[2], verts[3],
							side, CUBE_FACE::FRONT);
						currentSubChunk->numVertsUsed += 6;
					}

					// Back Face
					const int backBlockId = getBlockInternal(chunkData, x - 1, y, z, *this).id;
					const BlockFormat& backBlock = BlockMap::getBlock(backBlockId);
					if (!backBlockId || backBlock.isTransparent)
					{
						g_logger_assert(currentSubChunk->numVertsUsed + 6 <= World::MaxVertsPerSubChunk, "Ran out of room in sub-chunk! %d", currentSubChunk->numVertsUsed.load());
						loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[0], verts[1], verts[5], verts[4],
							side, CUBE_FACE::BACK);
						currentSubChunk->numVertsUsed += 6;
					}
				}
			}
		}

		currentSubChunk->uploadVertsToGpu = true;
	}

	static std::string getFormattedFilepath(int32 x, int32 z, const std::string& worldSavePath)
	{
		return worldSavePath + "/" +
			std::to_string(x) + "_" + std::to_string(z) + ".bin";
	}

	void Chunk::serialize(const std::string& worldSavePath)
	{
		std::string filepath = getFormattedFilepath(chunkCoordinates.x, chunkCoordinates.y, worldSavePath);
		FILE* fp = fopen(filepath.c_str(), "wb");
		fwrite(&chunkCoordinates, sizeof(glm::ivec2), 1, fp);
		fwrite(chunkData, sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth, 1, fp);
		fclose(fp);
	}

	void Chunk::deserialize(Block* newChunkData, const std::string& worldSavePath, int chunkX, int chunkZ)
	{
		std::string filepath = getFormattedFilepath(chunkX, chunkZ, worldSavePath);
		FILE* fp = fopen(filepath.c_str(), "rb");
		if (!fp)
		{
			g_logger_error("Could not open file '%s'", filepath.c_str());
			return;
		}

		fread(&chunkCoordinates, sizeof(glm::ivec2), 1, fp);
		chunkData = newChunkData;
		fread(chunkData, sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth, 1, fp);
		fclose(fp);
	}

	bool Chunk::exists(const std::string& worldSavePath, int chunkX, int chunkZ)
	{
		std::string filepath = getFormattedFilepath(chunkX, chunkZ, worldSavePath);
		return File::isFile(filepath.c_str());
	}

	static const int BASE_17_DEPTH = 17;
	static const int BASE_17_WIDTH = 17;
	static const int BASE_17_HEIGHT = 289;
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

	static Vertex compress(const glm::ivec3& vertex, const TextureFormat& texture, CUBE_FACE face, UV_INDEX uvIndex)
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

		data2 |= (((uint32)uvIndex << 0) & UV_INDEX_BITMASK);

		return {
			data1,
			data2
		};
	}

	static glm::ivec3 extractPosition(uint32 data)
	{
		int positionIndex = data & POSITION_INDEX_BITMASK;
		return toCoordinates(positionIndex);
	}

	static uint16 extractTexId(uint32 data)
	{
		return (data & TEX_ID_BITMASK) >> 17;
	}

	static CUBE_FACE extractFace(uint32 data)
	{
		return (CUBE_FACE)((data & FACE_BITMASK) >> 29);
	}

	static UV_INDEX extractCorner(uint32 data)
	{
		return (UV_INDEX)((data & UV_INDEX_BITMASK) >> 0);
	}

	static void loadBlock(
		Vertex* vertexData,
		const glm::ivec3& vert1,
		const glm::ivec3& vert2,
		const glm::ivec3& vert3,
		const glm::ivec3& vert4,
		const TextureFormat& texture,
		CUBE_FACE face)
	{
		vertexData[0] = compress(vert1, texture, face, UV_INDEX::BOTTOM_RIGHT);
		vertexData[1] = compress(vert2, texture, face, UV_INDEX::TOP_RIGHT);
		vertexData[2] = compress(vert3, texture, face, UV_INDEX::TOP_LEFT);

		vertexData[3] = compress(vert1, texture, face, UV_INDEX::BOTTOM_RIGHT);
		vertexData[4] = compress(vert3, texture, face, UV_INDEX::TOP_LEFT);
		vertexData[5] = compress(vert4, texture, face, UV_INDEX::BOTTOM_LEFT);

		// TODO: Remove this once you are confident you are getting the right values
		g_logger_assert(extractPosition(vertexData[0].data1) == vert1, "Failed Position.");
		g_logger_assert(extractPosition(vertexData[1].data1) == vert2, "Failed Position.");
		g_logger_assert(extractPosition(vertexData[2].data1) == vert3, "Failed Position.");

		g_logger_assert(extractPosition(vertexData[3].data1) == vert1, "Failed Position.");
		g_logger_assert(extractPosition(vertexData[4].data1) == vert3, "Failed Position.");
		g_logger_assert(extractPosition(vertexData[5].data1) == vert4, "Failed Position.");

		g_logger_assert(extractFace(vertexData[0].data1) == face, "Failed Face");
		g_logger_assert(extractFace(vertexData[1].data1) == face, "Failed Face");
		g_logger_assert(extractFace(vertexData[2].data1) == face, "Failed Face");
		g_logger_assert(extractFace(vertexData[3].data1) == face, "Failed Face");

		g_logger_assert(extractTexId(vertexData[0].data1) == texture.id, "Failed Texture Id");
		g_logger_assert(extractTexId(vertexData[1].data1) == texture.id, "Failed Texture Id");
		g_logger_assert(extractTexId(vertexData[2].data1) == texture.id, "Failed Texture Id");
		g_logger_assert(extractTexId(vertexData[3].data1) == texture.id, "Failed Texture Id");

		g_logger_assert(extractCorner(vertexData[0].data2) == UV_INDEX::BOTTOM_RIGHT, "Failed on uv index");
		g_logger_assert(extractCorner(vertexData[1].data2) == UV_INDEX::TOP_RIGHT, "Failed on uv index");
		g_logger_assert(extractCorner(vertexData[2].data2) == UV_INDEX::TOP_LEFT, "Failed on uv index");
		g_logger_assert(extractCorner(vertexData[3].data2) == UV_INDEX::BOTTOM_RIGHT, "Failed on uv index");
		g_logger_assert(extractCorner(vertexData[4].data2) == UV_INDEX::TOP_LEFT, "Failed on uv index");
		g_logger_assert(extractCorner(vertexData[5].data2) == UV_INDEX::BOTTOM_LEFT, "Failed on uv index");
	}
}