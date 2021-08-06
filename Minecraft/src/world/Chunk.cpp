#include "world/Chunk.h"
#include "utils/YamlExtended.h"
#include "world/BlockMap.h"
#include "utils/CMath.h"

namespace Minecraft
{
	static const int CHUNK_WIDTH = 16;
	static const int CHUNK_HEIGHT = 256;
	static const int CHUNK_DEPTH = 16;

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
		const Vertex& vert1,
		const Vertex& vert2,
		const Vertex& vert3,
		const Vertex& vert4,
		int vertexCursor,
		int32* elements,
		int elementIndexCursor,
		int elementCursor,
		const TextureFormat& texture,
		CUBE_FACE face);


	bool operator==(const Block& a, const Block& b)
	{
		return a.id == b.id;
	}

	void Chunk::info()
	{
		Logger::Info("%d size of chunk", sizeof(Block) * CHUNK_WIDTH * CHUNK_DEPTH * CHUNK_HEIGHT);
		Logger::Info("%d size of vertex data", sizeof(Vertex) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 24);
		Logger::Info("%d size of element data", sizeof(int32) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 36);
	}

	void Chunk::generate(int chunkX, int chunkZ, int32 seed)
	{
		const int worldChunkX = chunkX * 16;
		const int worldChunkZ = chunkZ * 16;

		worldPosition = { chunkX, chunkZ };

		chunkData = (Block*)AllocMem(sizeof(Block) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH);
		Memory::zeroMem(chunkData, sizeof(Block) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH);
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

		Vertex* vertexData = (Vertex*)AllocMem(sizeof(Vertex) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 24);
		int32* elements = (int32*)AllocMem(sizeof(int32) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 36);
		int vertexCursor = 0;
		int elementCursor = 0;
		int elementIndexCursor = 0;
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

					Vertex verts[8];
					verts[0].position = glm::vec3(
						(float)x - 0.5f + worldChunkX,
						(float)y + 0.5f,
						(float)z + 0.5f + worldChunkZ
					);
					verts[1].position = verts[0].position + glm::vec3(1, 0, 0);
					verts[2].position = verts[1].position - glm::vec3(0, 0, 1);
					verts[3].position = verts[0].position - glm::vec3(0, 0, 1);

					verts[4].position = verts[0].position - glm::vec3(0, 1, 0);
					verts[5].position = verts[1].position - glm::vec3(0, 1, 0);
					verts[6].position = verts[2].position - glm::vec3(0, 1, 0);
					verts[7].position = verts[3].position - glm::vec3(0, 1, 0);

					if (elementCursor >= UINT32_MAX - 24)
					{
						Logger::Assert(false, "UH OH");
					}

					// Top Face
					const int topBlockId = getBlock(chunkData, x, y + 1, z).id;
					const BlockFormat& topBlock = BlockMap::getBlock(topBlockId);
					if (!topBlockId || topBlock.isTransparent)
					{
						loadBlock(vertexData, verts[0], verts[1], verts[2], verts[3], vertexCursor,
							elements, elementIndexCursor, elementCursor, top, CUBE_FACE::TOP);
						vertexCursor += 4;
						elementIndexCursor += 6;
						elementCursor += 4;
					}

					// Bottom Face
					const int bottomBlockId = getBlock(chunkData, x, y - 1, z).id;
					const BlockFormat& bottomBlock = BlockMap::getBlock(bottomBlockId);
					if (!bottomBlockId || bottomBlock.isTransparent)
					{
						loadBlock(vertexData, verts[7], verts[6], verts[5], verts[4], vertexCursor,
							elements, elementIndexCursor, elementCursor, bottom, CUBE_FACE::BOTTOM);
						vertexCursor += 4;
						elementIndexCursor += 6;
						elementCursor += 4;
					}

					// Right Face
					const int rightBlockId = getBlock(chunkData, x, y, z + 1).id;
					const BlockFormat& rightBlock = BlockMap::getBlock(rightBlockId);
					if (!rightBlockId || rightBlock.isTransparent)
					{
						loadBlock(vertexData, verts[0], verts[4], verts[5], verts[1], vertexCursor,
							elements, elementIndexCursor, elementCursor, side, CUBE_FACE::RIGHT);
						vertexCursor += 4;
						elementIndexCursor += 6;
						elementCursor += 4;
					}

					// Left Face
					const int leftBlockId = getBlock(chunkData, x, y, z - 1).id;
					const BlockFormat& leftBlock = BlockMap::getBlock(leftBlockId);
					if (!leftBlockId || leftBlock.isTransparent)
					{
						loadBlock(vertexData, verts[2], verts[6], verts[7], verts[3], vertexCursor,
							elements, elementIndexCursor, elementCursor, side, CUBE_FACE::LEFT);
						vertexCursor += 4;
						elementIndexCursor += 6;
						elementCursor += 4;
					}

					// Forward Face
					const int forwardBlockId = getBlock(chunkData, x + 1, y, z).id;
					const BlockFormat& forwardBlock = BlockMap::getBlock(forwardBlockId);
					if (!forwardBlockId || forwardBlock.isTransparent)
					{
						loadBlock(vertexData, verts[1], verts[5], verts[6], verts[2], vertexCursor,
							elements, elementIndexCursor, elementCursor, side, CUBE_FACE::FRONT);
						vertexCursor += 4;
						elementIndexCursor += 6;
						elementCursor += 4;
					}

					// Back Face
					const int backBlockId = getBlock(chunkData, x - 1, y, z).id;
					const BlockFormat& backBlock = BlockMap::getBlock(backBlockId);
					if (!backBlockId || backBlock.isTransparent)
					{
						loadBlock(vertexData, verts[3], verts[7], verts[4], verts[0], vertexCursor,
							elements, elementIndexCursor, elementCursor, side, CUBE_FACE::BACK);
						vertexCursor += 4;
						elementIndexCursor += 6;
						elementCursor += 4;
					}
				}
			}
		}

		renderData.elements = elements;
		renderData.vertices = vertexData;
		renderData.vertexSizeBytes = sizeof(Vertex) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 24;
		renderData.elementSizeBytes = sizeof(uint32) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 36;
		renderData.numElements = CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 36;

		uploadToGPU(*this);
	}

	void Chunk::render()
	{
		glBindVertexArray(renderData.renderState.vao);
		glDrawElements(GL_TRIANGLES, renderData.numElements, GL_UNSIGNED_INT, nullptr);
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
			Logger::Error("Could not open file '%s'", filepath.c_str());
			return;
		}

		fread(&worldPosition, sizeof(glm::ivec2), 1, fp);
		chunkData = (Block*)AllocMem(sizeof(Block) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH);
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

		uint32 ebo;
		glGenBuffers(1, &ebo);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, renderData.elementSizeBytes, renderData.elements, GL_STATIC_DRAW);

		// 1b. then set our vertex attributes pointers
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
		glEnableVertexAttribArray(0);

		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, uv)));
		glEnableVertexAttribArray(1);

		glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, sizeof(Vertex), (void*)(offsetof(Vertex, face)));
		glEnableVertexAttribArray(2);
	}

	static void loadBlock(
		Vertex* vertexData,
		const Vertex& vert1,
		const Vertex& vert2,
		const Vertex& vert3,
		const Vertex& vert4,
		int vertexCursor,
		int32* elements,
		int elementIndexCursor,
		int elementCursor,
		const TextureFormat& texture,
		CUBE_FACE face)
	{
		vertexData[vertexCursor + 0].position = vert1.position;
		vertexData[vertexCursor + 1].position = vert2.position;
		vertexData[vertexCursor + 2].position = vert3.position;
		vertexData[vertexCursor + 3].position = vert4.position;

		elements[elementIndexCursor + 0] = elementCursor + 0;
		elements[elementIndexCursor + 1] = elementCursor + 1;
		elements[elementIndexCursor + 2] = elementCursor + 2;

		elements[elementIndexCursor + 3] = elementCursor + 0;
		elements[elementIndexCursor + 4] = elementCursor + 2;
		elements[elementIndexCursor + 5] = elementCursor + 3;

		vertexData[vertexCursor + 0].uv = texture.uvs[0];
		vertexData[vertexCursor + 1].uv = texture.uvs[1];
		vertexData[vertexCursor + 2].uv = texture.uvs[2];
		vertexData[vertexCursor + 3].uv = texture.uvs[3];

		vertexData[vertexCursor + 0].face = (uint32)face;
		vertexData[vertexCursor + 1].face = (uint32)face;
		vertexData[vertexCursor + 2].face = (uint32)face;
		vertexData[vertexCursor + 3].face = (uint32)face;
	}
}