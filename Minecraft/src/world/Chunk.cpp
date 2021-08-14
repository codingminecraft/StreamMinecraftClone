#include "world/Chunk.h"
#include "utils/YamlExtended.h"
#include "world/BlockMap.h"
#include "utils/CMath.h"
#include "core/File.h"

namespace Minecraft
{
	static const int CHUNK_WIDTH = 16;
	static const int CHUNK_HEIGHT = 256;
	static const int CHUNK_DEPTH = 16;

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
		return (x * CHUNK_DEPTH) + (y * CHUNK_HEIGHT) + z;
	}

	static const Block& getBlock(Block* data, int x, int y, int z)
	{
		int index = to1DArray(x, y, z);
		return x >= 16 || x < 0 || z >= 16 || z < 0 || y >= 256 || y < 0 ?
			BlockMap::NULL_BLOCK :
			data[index];
	}

	static void loadBlock(
		Vertex* vertexData,
		const glm::ivec3& vert1,
		const glm::ivec3& vert2,
		const glm::ivec3& vert3,
		const glm::ivec3& vert4,
		int vertexCursor,
		const TextureFormat& texture,
		CUBE_FACE face);


	bool operator==(const Block& a, const Block& b)
	{
		return a.id == b.id;
	}

	void Chunk::info()
	{
		g_logger_info("%d size of chunk", sizeof(Block) * CHUNK_WIDTH * CHUNK_DEPTH * CHUNK_HEIGHT);
		g_logger_info("%d size of vertex data", sizeof(Vertex) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 24);
	}

	void Chunk::generate(int chunkX, int chunkZ, int32 seed)
	{
		const int worldChunkX = chunkX * 16;
		const int worldChunkZ = chunkZ * 16;

		chunkCoordinates = { chunkX, chunkZ };

		chunkData = (Block*)g_memory_allocate(sizeof(Block) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH);
		g_memory_zeroMem(chunkData, sizeof(Block) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH);
		const SimplexNoise generator = SimplexNoise();
		for (int y = 0; y < CHUNK_HEIGHT; y++)
		{
			for (int x = 0; x < CHUNK_DEPTH; x++)
			{
				for (int z = 0; z < CHUNK_WIDTH; z++)
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
					int16 stoneHeight = (int16)(stoneHeightFloat * maxHeightFloat) / (127.0f);

					if (y == 0)
					{
						// Bedrock
						chunkData[arrayExpansion].id = 6;
					}
					else if (y < stoneHeight)
					{
						// Stone
						chunkData[arrayExpansion].id = 5;
					}
					else if (y < maxHeight)
					{
						// Dirt
						chunkData[arrayExpansion].id = 3;
					}
					else if (y == maxHeight)
					{
						// Green Concrete 
						chunkData[arrayExpansion].id = 4;
					}
				}
			}
		}

		// Count how many vertices we need
		numVertices = 0;
		for (int y = 0; y < CHUNK_HEIGHT; y++)
		{
			for (int x = 0; x < CHUNK_DEPTH; x++)
			{
				for (int z = 0; z < CHUNK_WIDTH; z++)
				{
					const Block& block = getBlock(chunkData, x, y, z);
					int blockId = block.id;

					if (block == BlockMap::NULL_BLOCK)
					{
						continue;
					}

					// Top Face
					const int topBlockId = getBlock(chunkData, x, y + 1, z).id;
					const BlockFormat& topBlock = BlockMap::getBlock(topBlockId);
					if (!topBlockId || topBlock.isTransparent)
					{
						numVertices += 6;
					}

					// Bottom Face
					const int bottomBlockId = getBlock(chunkData, x, y - 1, z).id;
					const BlockFormat& bottomBlock = BlockMap::getBlock(bottomBlockId);
					if (!bottomBlockId || bottomBlock.isTransparent)
					{
						numVertices += 6;
					}

					// Right Face
					const int rightBlockId = getBlock(chunkData, x, y, z + 1).id;
					const BlockFormat& rightBlock = BlockMap::getBlock(rightBlockId);
					if (!rightBlockId || rightBlock.isTransparent)
					{
						numVertices += 6;
					}

					// Left Face
					const int leftBlockId = getBlock(chunkData, x, y, z - 1).id;
					const BlockFormat& leftBlock = BlockMap::getBlock(leftBlockId);
					if (!leftBlockId || leftBlock.isTransparent)
					{
						numVertices += 6;
					}

					// Forward Face
					const int forwardBlockId = getBlock(chunkData, x + 1, y, z).id;
					const BlockFormat& forwardBlock = BlockMap::getBlock(forwardBlockId);
					if (!forwardBlockId || forwardBlock.isTransparent)
					{
						numVertices += 6;
					}

					// Back Face
					const int backBlockId = getBlock(chunkData, x - 1, y, z).id;
					const BlockFormat& backBlock = BlockMap::getBlock(backBlockId);
					if (!backBlockId || backBlock.isTransparent)
					{
						numVertices += 6;
					}
				}
			}
		}
	}

	void Chunk::generateRenderData()
	{
		g_logger_assert(numVertices > 0, "Cannot generate chunk render data for empty chunk.");

		const int worldChunkX = chunkCoordinates.x * 16;
		const int worldChunkZ = chunkCoordinates.y * 16;

		Vertex* vertexData = (Vertex*)g_memory_allocate(sizeof(Vertex) * numVertices);
		int vertexCursor = 0;
		int uvCursor = 0;

		for (int y = 0; y < CHUNK_HEIGHT; y++)
		{
			for (int x = 0; x < CHUNK_DEPTH; x++)
			{
				for (int z = 0; z < CHUNK_WIDTH; z++)
				{
					// 24 Vertices per cube
					const Block& block = getBlock(chunkData, x, y, z);
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
					const int topBlockId = getBlock(chunkData, x, y + 1, z).id;
					const BlockFormat& topBlock = BlockMap::getBlock(topBlockId);
					if (!topBlockId || topBlock.isTransparent)
					{
						loadBlock(vertexData, verts[5], verts[6], verts[7], verts[4], vertexCursor,
							top, CUBE_FACE::TOP);
						vertexCursor += 6;
					}

					// Bottom Face
					const int bottomBlockId = getBlock(chunkData, x, y - 1, z).id;
					const BlockFormat& bottomBlock = BlockMap::getBlock(bottomBlockId);
					if (!bottomBlockId || bottomBlock.isTransparent)
					{
						loadBlock(vertexData, verts[0], verts[3], verts[2], verts[1], vertexCursor,
							bottom, CUBE_FACE::BOTTOM);
						vertexCursor += 6;
					}

					// Right Face
					const int rightBlockId = getBlock(chunkData, x, y, z + 1).id;
					const BlockFormat& rightBlock = BlockMap::getBlock(rightBlockId);
					if (!rightBlockId || rightBlock.isTransparent)
					{
						loadBlock(vertexData, verts[2], verts[6], verts[5], verts[1], vertexCursor,
							side, CUBE_FACE::RIGHT);
						vertexCursor += 6;
					}

					// Left Face
					const int leftBlockId = getBlock(chunkData, x, y, z - 1).id;
					const BlockFormat& leftBlock = BlockMap::getBlock(leftBlockId);
					if (!leftBlockId || leftBlock.isTransparent)
					{
						loadBlock(vertexData, verts[0], verts[4], verts[7], verts[3], vertexCursor,
							side, CUBE_FACE::LEFT);
						vertexCursor += 6;
					}

					// Forward Face
					const int forwardBlockId = getBlock(chunkData, x + 1, y, z).id;
					const BlockFormat& forwardBlock = BlockMap::getBlock(forwardBlockId);
					if (!forwardBlockId || forwardBlock.isTransparent)
					{
						loadBlock(vertexData, verts[7], verts[6], verts[2], verts[3], vertexCursor,
							side, CUBE_FACE::FRONT);
						vertexCursor += 6;
					}

					// Back Face
					const int backBlockId = getBlock(chunkData, x - 1, y, z).id;
					const BlockFormat& backBlock = BlockMap::getBlock(backBlockId);
					if (!backBlockId || backBlock.isTransparent)
					{
						loadBlock(vertexData, verts[0], verts[1], verts[5], verts[4], vertexCursor,
							side, CUBE_FACE::BACK);
						vertexCursor += 6;
					}
				}
			}
		}

		renderData.vertices = vertexData;
		g_logger_assert(numVertices == vertexCursor, "We must have miscounted our vertices. Num Vertices does not match number of vertices added. Num Vertices '%d' Vertex Cursor '%d'", numVertices, vertexCursor);
	}

	void Chunk::render() const
	{
		glBindVertexArray(renderData.renderState.vao);
		glDrawArrays(GL_TRIANGLES, 0, numVertices);
	}

	void Chunk::unload()
	{
		//const std::lock_guard<std::mutex> booleanFlagsLock(lock);
		working = true;
		shouldUnload = true;
	}

	void Chunk::load()
	{
		//const std::lock_guard<std::mutex> booleanFlagsLock(lock);
		working = true;
		shouldLoad = true;
	}

	void Chunk::freeCpu()
	{
		if (loaded)
		{
			g_memory_free(renderData.vertices);
			g_memory_free(chunkData);

			renderData.vertices = nullptr;
			chunkData = nullptr;
		}
	}

	void Chunk::freeGpu()
	{
		if (loaded)
		{
			glDeleteBuffers(1, &renderData.renderState.vbo);
			glDeleteVertexArrays(1, &renderData.renderState.vao);

			renderData.renderState.vbo = 0;
			renderData.renderState.vao = 0;

			//const std::lock_guard<std::mutex> booleanFlagsLock(lock);
			loaded = false;
			shouldLoad = false;
			shouldUnload = false;
		}
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
		fwrite(&numVertices, sizeof(uint32), 1, fp);
		fwrite(chunkData, sizeof(Block) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH, 1, fp);
		fclose(fp);
	}

	void Chunk::deserialize(const std::string& worldSavePath, int chunkX, int chunkZ)
	{
		std::string filepath = getFormattedFilepath(chunkX, chunkZ, worldSavePath);
		FILE* fp = fopen(filepath.c_str(), "rb");
		if (!fp)
		{
			g_logger_error("Could not open file '%s'", filepath.c_str());
			return;
		}

		fread(&chunkCoordinates, sizeof(glm::ivec2), 1, fp);
		fread(&numVertices, sizeof(uint32), 1, fp);
		chunkData = (Block*)g_memory_allocate(sizeof(Block) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH);
		fread(chunkData, sizeof(Block) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH, 1, fp);
		fclose(fp);
	}

	bool Chunk::exists(const std::string& worldSavePath, int chunkX, int chunkZ)
	{
		std::string filepath = getFormattedFilepath(chunkX, chunkZ, worldSavePath);
		return File::isFile(filepath.c_str());
	}

	void Chunk::uploadToGPU()
	{
		// 1. Buffer the data
		glCreateVertexArrays(1, &renderData.renderState.vao);
		glBindVertexArray(renderData.renderState.vao);

		glGenBuffers(1, &renderData.renderState.vbo);

		// 1a. copy our vertices array in a buffer for OpenGL to use
		glBindBuffer(GL_ARRAY_BUFFER, renderData.renderState.vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * numVertices, renderData.vertices, GL_DYNAMIC_DRAW);

		// 1b. then set our vertex attributes pointers
		glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(Vertex), (void*)offsetof(Vertex, data1));
		glEnableVertexAttribArray(0);

		glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(Vertex), (void*)(offsetof(Vertex, data2)));
		glEnableVertexAttribArray(1);

		loaded = true;
		shouldLoad = false;
		shouldUnload = false;
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
		int vertexCursor,
		const TextureFormat& texture,
		CUBE_FACE face)
	{
		vertexData[vertexCursor + 0] = compress(vert1, texture, face, UV_INDEX::BOTTOM_RIGHT);
		vertexData[vertexCursor + 1] = compress(vert2, texture, face, UV_INDEX::TOP_RIGHT);
		vertexData[vertexCursor + 2] = compress(vert3, texture, face, UV_INDEX::TOP_LEFT);

		vertexData[vertexCursor + 3] = compress(vert1, texture, face, UV_INDEX::BOTTOM_RIGHT);
		vertexData[vertexCursor + 4] = compress(vert3, texture, face, UV_INDEX::TOP_LEFT);
		vertexData[vertexCursor + 5] = compress(vert4, texture, face, UV_INDEX::BOTTOM_LEFT);

		// TODO: Remove this once you are confident you are getting the right values
		g_logger_assert(extractPosition(vertexData[vertexCursor + 0].data1) == vert1, "Failed Position.");
		g_logger_assert(extractPosition(vertexData[vertexCursor + 1].data1) == vert2, "Failed Position.");
		g_logger_assert(extractPosition(vertexData[vertexCursor + 2].data1) == vert3, "Failed Position.");

		g_logger_assert(extractPosition(vertexData[vertexCursor + 3].data1) == vert1, "Failed Position.");
		g_logger_assert(extractPosition(vertexData[vertexCursor + 4].data1) == vert3, "Failed Position.");
		g_logger_assert(extractPosition(vertexData[vertexCursor + 5].data1) == vert4, "Failed Position.");

		g_logger_assert(extractFace(vertexData[vertexCursor + 0].data1) == face, "Failed Face");
		g_logger_assert(extractFace(vertexData[vertexCursor + 1].data1) == face, "Failed Face");
		g_logger_assert(extractFace(vertexData[vertexCursor + 2].data1) == face, "Failed Face");
		g_logger_assert(extractFace(vertexData[vertexCursor + 3].data1) == face, "Failed Face");

		g_logger_assert(extractTexId(vertexData[vertexCursor + 0].data1) == texture.id, "Failed Texture Id");
		g_logger_assert(extractTexId(vertexData[vertexCursor + 1].data1) == texture.id, "Failed Texture Id");
		g_logger_assert(extractTexId(vertexData[vertexCursor + 2].data1) == texture.id, "Failed Texture Id");
		g_logger_assert(extractTexId(vertexData[vertexCursor + 3].data1) == texture.id, "Failed Texture Id");

		g_logger_assert(extractCorner(vertexData[vertexCursor + 0].data2) == UV_INDEX::BOTTOM_RIGHT, "Failed on uv index");
		g_logger_assert(extractCorner(vertexData[vertexCursor + 1].data2) == UV_INDEX::TOP_RIGHT, "Failed on uv index");
		g_logger_assert(extractCorner(vertexData[vertexCursor + 2].data2) == UV_INDEX::TOP_LEFT, "Failed on uv index");
		g_logger_assert(extractCorner(vertexData[vertexCursor + 3].data2) == UV_INDEX::BOTTOM_RIGHT, "Failed on uv index");
		g_logger_assert(extractCorner(vertexData[vertexCursor + 4].data2) == UV_INDEX::TOP_LEFT, "Failed on uv index");
		g_logger_assert(extractCorner(vertexData[vertexCursor + 5].data2) == UV_INDEX::BOTTOM_LEFT, "Failed on uv index");
	}
}