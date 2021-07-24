#include "world/Chunk.h"
#include "utils/YamlExtended.h"
#include "world/BlockMap.h"

#include <vector>
#include <unordered_map>

namespace Minecraft
{
	static const int CHUNK_WIDTH = 16;
	static const int CHUNK_HEIGHT = 256;
	static const int CHUNK_DEPTH = 16;

	enum class CUBE_FACE : int32
	{
		TOP,
		SIDE,
		BOTTOM,
		RIGHT,
		LEFT,
		BACK
	};

	static int to1DArray(int x, int y, int z)
	{
		//return z * (CHUNK_DEPTH * CHUNK_HEIGHT) + (y + CHUNK_HEIGHT * x);
		return (x * CHUNK_DEPTH) + (y * CHUNK_HEIGHT) + z;
	}

	static int getBlock(int16* data, int x, int y, int z)
	{
		int index = to1DArray(x, y, z);
		return x >= 16 || x < 0 || z >= 16 || z < 0 || y >= 256 || y < 0?
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
		const TextureFormat& texture);

	ChunkRenderData Chunk::generate()
	{
		chunkData = (int16*)AllocMem(sizeof(int16) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH);
		Memory::zeroMem(chunkData, sizeof(int16) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH);
		for (int y = 0; y < CHUNK_HEIGHT; y++)
		{
			for (int x = 0; x <	CHUNK_DEPTH; x++)
			{
				for (int z = 0; z < CHUNK_WIDTH; z++)
				{
					// 24 Vertices per cube
					const int arrayExpansion = to1DArray(x, y, z);
					if (y < 10)
					{
						chunkData[arrayExpansion] = 2;
					}
					else if (y < 255)
					{
						chunkData[arrayExpansion] = 3;
					}
					else
					{
						chunkData[arrayExpansion] = 4;
					}
				}
			}
		}

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
					const int blockId = getBlock(chunkData, x, y, z);

					const BlockFormat& blockFormat = BlockMap::getBlock(blockId);
					const TextureFormat& side = BlockMap::getTextureFormat(blockFormat.sideTexture);
					const TextureFormat& top = BlockMap::getTextureFormat(blockFormat.topTexture);
					const TextureFormat& bottom = BlockMap::getTextureFormat(blockFormat.bottomTexture);

					Vertex verts[8];
					verts[0].position = glm::vec3(
						(float)x - 0.5f,
						(float)y + 0.5f,
						(float)z + 0.5f
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
						Logger::Info("UH OH");
					}
					//for (int i = 0; i < 6; i++)
					//{
					//	elements[elementIndexCursor + (i * 6) + 0] = elementCursor + 0 + (i * 4);
					//	elements[elementIndexCursor + (i * 6) + 1] = elementCursor + 1 + (i * 4);
					//	elements[elementIndexCursor + (i * 6) + 2] = elementCursor + 2 + (i * 4);

					//	elements[elementIndexCursor + (i * 6) + 3] = elementCursor + 0 + (i * 4);
					//	elements[elementIndexCursor + (i * 6) + 4] = elementCursor + 2 + (i * 4);
					//	elements[elementIndexCursor + (i * 6) + 5] = elementCursor + 3 + (i * 4);
					//}
					//elementIndexCursor += 36;
					//elementCursor += 24;

					//const int uvIndex = arrayExpansion * 24;
					//for (int i = 0; i < 6; i++)
					//{
					//	const TextureFormat& textureFormat =
					//		i == 0 ? top :
					//		i > 0 && i < 5 ? side :
					//		bottom;
					//	vertexData[uvIndex + (i * 4)].uv = textureFormat.uvs[0];
					//	vertexData[uvIndex + (i * 4) + 1].uv = textureFormat.uvs[1];
					//	vertexData[uvIndex + (i * 4) + 2].uv = textureFormat.uvs[2];
					//	vertexData[uvIndex + (i * 4) + 3].uv = textureFormat.uvs[3];
					//}

					// Top Face
					const int topBlockId = getBlock(chunkData, x, y + 1, z);
					const BlockFormat& topBlock = BlockMap::getBlock(topBlockId);
					if (!topBlockId || topBlock.isTransparent)
					{
						loadBlock(vertexData, verts[0], verts[1], verts[2], verts[3], vertexCursor,
							elements, elementIndexCursor, elementCursor, top);
						vertexCursor += 4;
						elementIndexCursor += 6;
						elementCursor += 4;
					}

					// Bottom Face
					const int bottomBlockId = getBlock(chunkData, x, y - 1, z);
					const BlockFormat& bottomBlock = BlockMap::getBlock(bottomBlockId);
					if (!bottomBlockId || bottomBlock.isTransparent)
					{
						loadBlock(vertexData, verts[7], verts[6], verts[5], verts[4], vertexCursor,
							elements, elementIndexCursor, elementCursor, bottom);
						vertexCursor += 4;
						elementIndexCursor += 6;
						elementCursor += 4;
					}

					// Right Face
					const int rightBlockId = getBlock(chunkData, x, y, z + 1);
					const BlockFormat& rightBlock = BlockMap::getBlock(rightBlockId);
					if (!rightBlockId || rightBlock.isTransparent)
					{
						loadBlock(vertexData, verts[0], verts[4], verts[5], verts[1], vertexCursor,
							elements, elementIndexCursor, elementCursor, side);
						vertexCursor += 4;
						elementIndexCursor += 6;
						elementCursor += 4;
					}

					// Left Face
					const int leftBlockId = getBlock(chunkData, x, y, z - 1);
					const BlockFormat& leftBlock = BlockMap::getBlock(leftBlockId);
					if (!leftBlockId || leftBlock.isTransparent)
					{
						loadBlock(vertexData, verts[2], verts[6], verts[7], verts[3], vertexCursor,
							elements, elementIndexCursor, elementCursor, side);
						vertexCursor += 4;
						elementIndexCursor += 6;
						elementCursor += 4;
					}

					// Forward Face
					const int forwardBlockId = getBlock(chunkData, x + 1, y, z);
					const BlockFormat& forwardBlock = BlockMap::getBlock(forwardBlockId);
					if (!forwardBlockId || forwardBlock.isTransparent)
					{
						loadBlock(vertexData, verts[1], verts[5], verts[6], verts[2], vertexCursor,
							elements, elementIndexCursor, elementCursor, side);
						vertexCursor += 4;
						elementIndexCursor += 6;
						elementCursor += 4;
					}

					// Back Face
					const int backBlockId = getBlock(chunkData, x - 1, y, z);
					const BlockFormat& backBlock = BlockMap::getBlock(backBlockId);
					if (!backBlockId || backBlock.isTransparent)
					{
						loadBlock(vertexData, verts[3], verts[7], verts[4], verts[0], vertexCursor,
							elements, elementIndexCursor, elementCursor, side);
						vertexCursor += 4;
						elementIndexCursor += 6;
						elementCursor += 4;
					}
				}
			}
		}

		ChunkRenderData ret;
		ret.elements = elements;
		ret.vertices = vertexData;
		ret.vertexSizeBytes = sizeof(Vertex) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 24;
		ret.elementSizeBytes = sizeof(uint32) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 36;
		ret.numElements = CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH * 36;
		return ret;
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
		const TextureFormat& texture)
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
	}
}