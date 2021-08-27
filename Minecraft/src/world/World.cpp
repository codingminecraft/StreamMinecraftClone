#include "core.h"
#include "world/World.h"
#include "world/ChunkManager.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Camera.h"
#include "utils/TexturePacker.h"
#include "core/Input.h"
#include "core/Application.h"
#include "world/BlockMap.h"
#include "gameplay/PlayerController.h"
#include "core/File.h"

namespace Minecraft
{
	namespace World
	{
		// Internal declarations
		static void loadWorldTexture();
		static void checkChunkRadius();
		static void synchronizeChunks();

		// Members
		static RenderState renderState;
		static Camera camera;
		static PlayerController playerController;
		static Shader shader;
		static Texture worldTexture;
		static std::unordered_set<glm::ivec2> loadedChunkPositions;
		static Chunk chunks[ChunkCapacity];
		static const std::string worldSavePath = "world";

		static int32 seed = 0;

		void init()
		{
			srand((unsigned long)time(NULL));
			seed = (int32)(((float)rand() / (float)RAND_MAX) * 1000.0f);
			g_logger_info("World seed: %d", seed);

			// Initialize blocks
			//TexturePacker::packTextures("C:/dev/C++/MinecraftClone/assets/images/block", "textureFormat.yaml");
			g_memory_zeroMem(chunks, sizeof(Chunk) * ChunkCapacity);

			BlockMap::loadBlocks("textureFormat.yaml", "blockFormats.yaml");
			BlockMap::uploadTextureCoordinateMapToGpu();
			shader = NShader::createShader("C:/dev/C++/MinecraftClone/assets/shaders/default.glsl");
			loadWorldTexture();


			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_BUFFER, BlockMap::getTextureCoordinatesTextureId());
			NShader::uploadInt(shader, "uTexCoordTexture", 1);

			// Create a chunk
			File::createDirIfNotExists(worldSavePath.c_str());
			Chunk::info();

			// Setup camera
			camera.position = glm::vec3(0, 257.0f, 1.0f);
			camera.fov = 45.0f;
			camera.orientation = glm::vec3(0.0f, 0.0f, 0.0f);
			playerController.init(&camera);

			ChunkManager::init(seed);
			checkChunkRadius();
		}

		void update(float dt)
		{
			glm::mat4 projection = camera.calculateProjectionMatrix();
			glm::mat4 view = camera.calculateViewMatrix();
			NShader::uploadMat4(shader, "uProjection", projection);
			NShader::uploadMat4(shader, "uView", view);
			NShader::uploadVec3(shader, "uSunPosition", glm::vec3{ 1, 355, 1 });

			playerController.update(dt);

			if (Input::isKeyPressed(GLFW_KEY_F1))
			{
				Application::lockCursor(false);
			}
			else if (Input::isKeyPressed(GLFW_KEY_F2))
			{
				Application::lockCursor(true);
			}

			const glm::vec3& playerPosition = playerController.playerCamera->position;
			glm::ivec2 playerPositionInChunkCoords = glm::ivec2{
				playerPosition.x / 16,
				playerPosition.z / 16
			};
			for (int i = 0; i < ChunkCapacity; i++)
			{
				Chunk& chunk = chunks[i];
				if (chunk.loaded)
				{
					NShader::uploadIVec2(shader, "uChunkPos", chunk.chunkCoordinates);
					chunk.render();

					const glm::ivec2 localChunkPos = chunk.chunkCoordinates - playerPositionInChunkCoords;
					bool hasBeenQueued = loadedChunkPositions.find(chunk.chunkCoordinates) == loadedChunkPositions.end();
					if (!hasBeenQueued && (localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) >= ChunkRadius * ChunkRadius)
					{
						ChunkManager::queueDeleteChunk(chunk);
						loadedChunkPositions.erase(chunk.chunkCoordinates);
					}
				}
			}

			checkChunkRadius();
			synchronizeChunks();
		}

		void cleanup()
		{
			ChunkManager::free();

			for (int i = 0; i < ChunkCapacity; i++)
			{
				Chunk& chunk = chunks[i];
				if (chunk.loaded)
				{
					chunk.freeCpu();
					chunk.freeGpu();
				}
			}
		}

		static bool exists(const glm::ivec2& position)
		{
			for (Chunk& chunk : chunks)
			{
				if (chunk.chunkCoordinates == position)
				{
					return true;
				}
			}

			return false;
		}

		static void checkChunkRadius()
		{
			const glm::vec3 playerPosition = playerController.playerCamera->position;
			glm::ivec2 position = glm::ivec2{
				playerPosition.x / 16,
				playerPosition.z / 16
			};
			glm::ivec2 playerPosChunkCoords = position;

			bool needsWork = false;
			// 0 RIGHT, 1 UP, 2 LEFT, 3 DOWN
			uint8 direction = 0;
			uint8 distance = 1;
			uint8 timeToChangeDistance = 0;
			uint8 travelled = 0;
			while (true)
			{
				glm::ivec2 localPos = playerPosChunkCoords - position;
				if ((localPos.x * localPos.x) + (localPos.y * localPos.y) < ChunkRadius * ChunkRadius)
				{
					bool alreadyExists = exists(position);
					auto iter = loadedChunkPositions.find(position);
					if (!alreadyExists && iter == loadedChunkPositions.end())
					{
						ChunkManager::queueCreateChunk(position.x, position.y);
						loadedChunkPositions.insert(position);
					}
				}

				switch (direction)
				{
				case 0:
					position.x++;
					break;
				case 1:
					position.y++;
					break;
				case 2:
					position.x--;
					break;
				case 3:
					position.y--;
					break;
				}

				travelled++;
				if (travelled >= distance)
				{
					direction = (direction + 1) % 4;
					travelled = 0;

					timeToChangeDistance++;
					if (timeToChangeDistance > 1)
					{
						timeToChangeDistance = 0;
						distance++;
					}

					if (distance > ChunkRadius * 2)
					{
						break;
					}
				}
			}
		}

		static void synchronizeChunks()
		{
			std::vector<Chunk> readyChunks = ChunkManager::getReadyChunks();
			if (readyChunks.size() > 0)
			{
				for (Chunk& chunk : readyChunks)
				{
					if (chunk.loaded)
					{
						g_logger_info("Unloading from GPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
						chunk.freeGpu();

						for (int i = 0; i < ChunkCapacity; i++)
						{
							if (chunks[i].chunkCoordinates == chunk.chunkCoordinates)
							{
								chunks[i] = chunk;
								break;
							}
						}
					}
					else
					{
						g_logger_info("Uploading to GPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
						chunk.uploadToGPU();

						for (int i = 0; i < ChunkCapacity; i++)
						{
							if (!chunks[i].loaded)
							{
								chunks[i] = chunk;
								break;
							}
						}
					}
				}
			}
		}

		static void loadWorldTexture()
		{
			worldTexture = TextureBuilder()
				.setFormat(ByteFormat::RGBA8_UI)
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setFilepath("test.png")
				.generate(true);

			// Upload the world texture
			glActiveTexture(GL_TEXTURE0);
			worldTexture.bind();
			NShader::uploadInt(shader, "uTexture", 0);

			glUseProgram(shader.programId);
		}
	}
}