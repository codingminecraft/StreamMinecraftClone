#include "world/Chunk.h"

namespace Minecraft
{
	const int CHUNK_WIDTH = 16;
	const int CHUNK_HEIGHT = 256;
	const int CHUNK_LENGTH = 16;

	ChunkRenderData Chunk::generate()
	{
		chunkData = (int16*)AllocMem(sizeof(int16) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_LENGTH);
		Memory::ZeroMem(chunkData, sizeof(int16) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_LENGTH);

		Vertex* vertexData = (Vertex*)AllocMem(sizeof(Vertex) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_LENGTH * 8);
		int32* elements = (int32*)AllocMem(sizeof(int32) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_LENGTH * 36);
		for (int x = 0; x < CHUNK_WIDTH; x++)
		{
			for (int y = 0; y < CHUNK_HEIGHT; y++)
			{
				for (int z = 0; z < CHUNK_LENGTH; z++)
				{
					Vertex verts[8];
					verts[0].position = glm::vec3(
						(float)x + 0.5f,
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

					verts[0].uv = glm::vec2(1, 1);
					verts[1].uv = glm::vec2(1, 0);
					verts[2].uv = glm::vec2(0, 0);
					verts[3].uv = glm::vec2(0, 1);

					verts[4].uv = glm::vec2(1, 1);
					verts[5].uv = glm::vec2(1, 0);
					verts[6].uv = glm::vec2(0, 0);
					verts[7].uv = glm::vec2(0, 1);

					for (int i = 0; i < 8; i++)
					{
						vertexData[(x + CHUNK_WIDTH * (z + CHUNK_LENGTH * y)) * 8 + i] = verts[i];
					}

					// 0 1 2
					// 0 2 3
					int elementIndex = (x + CHUNK_WIDTH * (z + CHUNK_LENGTH * y)) * 36;
					for (int i = 0; i < 6; i++)
					{
						elements[elementIndex++] = 0;
						elements[elementIndex++] = 1;
						elements[elementIndex++] = 2;

						elements[elementIndex++] = 0;
						elements[elementIndex++] = 2;
						elements[elementIndex++] = 3;
					}
				}
			}

			ChunkRenderData ret;
			ret.elements = elements;
			ret.vertices = vertexData;
			ret.vertexSizeBytes = sizeof(Vertex) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_LENGTH * 8;
			ret.elementSizeBytes = sizeof(int32) * CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_LENGTH * 36;
			return ret;
		}
	}
}