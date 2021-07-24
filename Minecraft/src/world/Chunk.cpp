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

	ChunkRenderData Chunk::generate()
	{
		chunkData = (int16*)AllocMem(sizeof(int16) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH);
		Memory::zeroMem(chunkData, sizeof(int16) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH);
		for (int x = 0; x < CHUNK_WIDTH; x++)
		{
			for (int z = 0; z < CHUNK_DEPTH; z++)
			{
				for (int y = 0; y < CHUNK_HEIGHT; y++)
				{
					// 24 Vertices per cube
					const int arrayExpansion = (x * (CHUNK_DEPTH * CHUNK_HEIGHT) + (y + CHUNK_HEIGHT * z));
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
		for (int x = 0; x < CHUNK_WIDTH; x++)
		{
			for (int z = 0; z < CHUNK_DEPTH; z++)
			{
				for (int y = 0; y < CHUNK_HEIGHT; y++)
				{
					// 24 Vertices per cube
					const int arrayExpansion = (x * (CHUNK_DEPTH * CHUNK_HEIGHT) + (y + CHUNK_HEIGHT * z));
					const int blockId = chunkData[arrayExpansion];

					const BlockFormat& blockFormat = BlockMap::getBlock(chunkData[arrayExpansion]);
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

					const int vertexOffset = arrayExpansion * 24;
					const int elementIndex = arrayExpansion * 36;

					const int elementOffset = arrayExpansion * 24;
					if (elementOffset >= UINT32_MAX - 24)
					{
						Logger::Info("UH OH");
					}
					for (int i = 0; i < 6; i++)
					{
						elements[elementIndex + (i * 6) + 0] = elementOffset + 0 + (i * 4);
						elements[elementIndex + (i * 6) + 1] = elementOffset + 1 + (i * 4);
						elements[elementIndex + (i * 6) + 2] = elementOffset + 2 + (i * 4);

						elements[elementIndex + (i * 6) + 3] = elementOffset + 0 + (i * 4);
						elements[elementIndex + (i * 6) + 4] = elementOffset + 2 + (i * 4);
						elements[elementIndex + (i * 6) + 5] = elementOffset + 3 + (i * 4);
					}

					const int uvIndex = arrayExpansion * 24;
					for (int i = 0; i < 6; i++)
					{
						const TextureFormat& textureFormat = 
							i == 0 ? top :
							i > 0 && i < 5 ? side :
							bottom;
						vertexData[uvIndex + (i * 4)].uv = textureFormat.uvs[0];
						vertexData[uvIndex + (i * 4) + 1].uv = textureFormat.uvs[1];
						vertexData[uvIndex + (i * 4) + 2].uv = textureFormat.uvs[2];
						vertexData[uvIndex + (i * 4) + 3].uv = textureFormat.uvs[3];
					}

					// Top Face
					vertexData[vertexOffset + 0].position = verts[0].position;
					vertexData[vertexOffset + 1].position = verts[1].position;
					vertexData[vertexOffset + 2].position = verts[2].position;
					vertexData[vertexOffset + 3].position = verts[3].position;

					// Right Face
					vertexData[vertexOffset + 4].position = verts[0].position;
					vertexData[vertexOffset + 5].position = verts[4].position;
					vertexData[vertexOffset + 6].position = verts[5].position;
					vertexData[vertexOffset + 7].position = verts[1].position;

					// Forward Face
					vertexData[vertexOffset + 8].position = verts[1].position;
					vertexData[vertexOffset + 9].position = verts[5].position;
					vertexData[vertexOffset + 10].position = verts[6].position;
					vertexData[vertexOffset + 11].position = verts[2].position;

					// Left Face
					vertexData[vertexOffset + 12].position = verts[2].position;
					vertexData[vertexOffset + 13].position = verts[6].position;
					vertexData[vertexOffset + 14].position = verts[7].position;
					vertexData[vertexOffset + 15].position = verts[3].position;

					// Back Face
					vertexData[vertexOffset + 16].position = verts[3].position;
					vertexData[vertexOffset + 17].position = verts[7].position;
					vertexData[vertexOffset + 18].position = verts[4].position;
					vertexData[vertexOffset + 19].position = verts[0].position;

					// Bottom Face
					vertexData[vertexOffset + 20].position = verts[7].position;
					vertexData[vertexOffset + 21].position = verts[6].position;
					vertexData[vertexOffset + 22].position = verts[5].position;
					vertexData[vertexOffset + 23].position = verts[4].position;
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
}