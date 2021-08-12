#include "world/Chunk.h"
#include "utils/YamlExtended.h"
#include "world/BlockMap.h"
#include "utils/CMath.h"

namespace Minecraft
{
	static const int CHUNK_WIDTH = 16;
	static const int CHUNK_HEIGHT = 256;
	static const int CHUNK_DEPTH = 16;

	static const int POSITION_INDEX_BITMASK = 0x1FFFF;
	static const int TEX_ID_BITMASK = 0x1FFE0000;
	static const int FACE_BITMASK = 0xE0000000;

	enum class CUBE_FACE : uint32
	{
		LEFT = 0,
		RIGHT = 1,
		BOTTOM = 2,
		TOP = 3,
		BACK = 4,
		FRONT = 5
	};

	static void uploadToGPU(Chunk& chunk);

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

		worldPosition = { chunkX, chunkZ };

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
	}

	void Chunk::generateRenderData() 
	{
		const int worldChunkX = worldPosition.x * 16;
		const int worldChunkZ = worldPosition.y * 16;

		Vertex* vertexData = (Vertex*)g_memory_allocate(sizeof(Vertex) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 24);
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
		renderData.vertexSizeBytes = sizeof(Vertex) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 24;
		renderData.numVertices = vertexCursor;

		uploadToGPU(*this);
	}

	void Chunk::render() const
	{
		glBindVertexArray(renderData.renderState.vao);
		glDrawArrays(GL_TRIANGLES, 0, renderData.numVertices);
	}

	void Chunk::free()
	{
		glDeleteBuffers(1, &renderData.renderState.vbo);
		glDeleteVertexArrays(1, &renderData.renderState.vao);

		g_memory_free(renderData.vertices);
		g_memory_free(chunkData);
	}

	static std::string getFormattedFilepath(int32 x, int32 z, const std::string& worldSavePath)
	{
		return worldSavePath + "/" +
			std::to_string(x) + "_" + std::to_string(z) + ".bin";
	}

	void Chunk::serialize(const std::string& worldSavePath)
	{
		std::string filepath = getFormattedFilepath(worldPosition.x, worldPosition.y, worldSavePath);
		FILE* fp = fopen(filepath.c_str(), "wb");
		fwrite(&worldPosition, sizeof(glm::ivec2), 1, fp);
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

		fread(&worldPosition, sizeof(glm::ivec2), 1, fp);
		chunkData = (Block*)g_memory_allocate(sizeof(Block) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH);
		fread(chunkData, sizeof(Block) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH, 1, fp);
		fclose(fp);
	}

	static void uploadToGPU(Chunk& chunk)
	{
		ChunkRenderData& renderData = chunk.renderData;

		// 1. Buffer the data
		glCreateVertexArrays(1, &renderData.renderState.vao);
		glBindVertexArray(renderData.renderState.vao);

		glGenBuffers(1, &renderData.renderState.vbo);

		// 1a. copy our vertices array in a buffer for OpenGL to use
		glBindBuffer(GL_ARRAY_BUFFER, renderData.renderState.vbo);
		glBufferData(GL_ARRAY_BUFFER, renderData.vertexSizeBytes, renderData.vertices, GL_STATIC_DRAW);

		// 1b. then set our vertex attributes pointers
		glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(Vertex), (void*)offsetof(Vertex, data));
		glEnableVertexAttribArray(0);

		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, uv)));
		glEnableVertexAttribArray(1);
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

	static uint32 compress(const glm::ivec3& vertex, const TextureFormat& texture, CUBE_FACE face)
	{
		// Bits  0-16 position index
		// Bits 17-28 texId
		// Bits 29-31 normalDir face value
		uint32 data = 0;

		int positionIndex = toCompressedVec3(vertex.x, vertex.y, vertex.z);
		data |= ((positionIndex << 0) & POSITION_INDEX_BITMASK);
		data |= ((texture.id << 17) & TEX_ID_BITMASK);
		data |= ((uint32)face << 29) & FACE_BITMASK;

		return data;
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
		vertexData[vertexCursor + 0].data = compress(vert1, texture, face);
		vertexData[vertexCursor + 1].data = compress(vert2, texture, face);
		vertexData[vertexCursor + 2].data = compress(vert3, texture, face);

		vertexData[vertexCursor + 3].data = compress(vert1, texture, face);
		vertexData[vertexCursor + 4].data = compress(vert3, texture, face);
		vertexData[vertexCursor + 5].data = compress(vert4, texture, face);

		vertexData[vertexCursor + 0].uv = texture.uvs[0];
		vertexData[vertexCursor + 1].uv = texture.uvs[1];
		vertexData[vertexCursor + 2].uv = texture.uvs[2];
		vertexData[vertexCursor + 3].uv = texture.uvs[0];
		vertexData[vertexCursor + 4].uv = texture.uvs[2];
		vertexData[vertexCursor + 5].uv = texture.uvs[3];

		// TODO: Remove this once you are confident you are getting the right values
		g_logger_assert(extractPosition(vertexData[vertexCursor + 0].data) == vert1, "Failed Position.");
		g_logger_assert(extractPosition(vertexData[vertexCursor + 1].data) == vert2, "Failed Position.");
		g_logger_assert(extractPosition(vertexData[vertexCursor + 2].data) == vert3, "Failed Position.");

		g_logger_assert(extractPosition(vertexData[vertexCursor + 3].data) == vert1, "Failed Position.");
		g_logger_assert(extractPosition(vertexData[vertexCursor + 4].data) == vert3, "Failed Position.");
		g_logger_assert(extractPosition(vertexData[vertexCursor + 5].data) == vert4, "Failed Position.");

		g_logger_assert(extractFace(vertexData[vertexCursor + 0].data) == face, "Failed Face");
		g_logger_assert(extractFace(vertexData[vertexCursor + 1].data) == face, "Failed Face");
		g_logger_assert(extractFace(vertexData[vertexCursor + 2].data) == face, "Failed Face");
		g_logger_assert(extractFace(vertexData[vertexCursor + 3].data) == face, "Failed Face");

		g_logger_assert(extractTexId(vertexData[vertexCursor + 0].data) == texture.id, "Failed Texture Id");
		g_logger_assert(extractTexId(vertexData[vertexCursor + 1].data) == texture.id, "Failed Texture Id");
		g_logger_assert(extractTexId(vertexData[vertexCursor + 2].data) == texture.id, "Failed Texture Id");
		g_logger_assert(extractTexId(vertexData[vertexCursor + 3].data) == texture.id, "Failed Texture Id");
	}
}