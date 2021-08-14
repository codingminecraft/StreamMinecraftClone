#include "core.h"
#include "world/World.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Camera.h"
#include "utils/TexturePacker.h"
#include "core/Input.h"
#include "core/Application.h"
#include "world/BlockMap.h"
#include "gameplay/PlayerController.h"
#include "core/File.h"

#include <random>

namespace Minecraft
{
	namespace World
	{
		// Internal declarations
		static void loadWorldTexture();
		static void checkChunkRadius();
		static void unloadChunks();

		// Members
		static RenderState renderState;
		static Camera camera;
		static PlayerController playerController;
		static Shader shader;
		static Texture worldTexture;
		static std::vector<Chunk> loadedChunks;
		static std::unordered_set<glm::ivec2> loadedChunkPositions;
		static uint16 chunkRadius = 6;
		static const std::string worldSavePath = "world";

		static int32 seed = 0;

		void init()
		{
			srand((unsigned long)time(NULL));
			seed = (int32)(((float)rand() / (float)RAND_MAX) * 1000.0f);
			g_logger_info("World seed: %d", seed);

			// Initialize blocks
			//TexturePacker::packTextures("C:/dev/C++/MinecraftClone/assets/images/block", "textureFormat.yaml");
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
			for (Chunk& chunk : loadedChunks)
			{
				NShader::uploadIVec2(shader, "uChunkPos", chunk.chunkCoordinates);
				chunk.render();

				const glm::ivec2 localChunkPos = chunk.chunkCoordinates - playerPositionInChunkCoords;
				if ((localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) >= chunkRadius * chunkRadius)
				{
					chunk.unload();
					loadedChunkPositions.erase(chunk.chunkCoordinates);
				}
			}

			unloadChunks();
			checkChunkRadius();
		}

		void cleanup()
		{
			for (Chunk& chunk : loadedChunks)
			{
				chunk.free();
			}
		}

		static void checkChunkRadius()
		{
			const glm::vec3 playerPosition = playerController.playerCamera->position;
			glm::ivec2 playerPositionInChunkCoords = glm::ivec2{
				playerPosition.x / 16,
				playerPosition.z / 16
			};

			int startX = playerPositionInChunkCoords.x - (chunkRadius / 2);
			int endX = playerPositionInChunkCoords.x + (chunkRadius / 2);
			int startZ = playerPositionInChunkCoords.y - (chunkRadius / 2);
			int endZ = playerPositionInChunkCoords.y + (chunkRadius / 2);

			for (int z = startZ; z <= endZ; z++)
			{
				for (int x = startX; x <= endX; x++)
				{
					auto iter = loadedChunkPositions.find(glm::ivec2{ x, z });
					if (iter == loadedChunkPositions.end())
					{
						g_logger_info("Loading chunk<%d, %d>", x, z);
						Chunk chunk;
						if (Chunk::exists(worldSavePath, x, z))
						{
							chunk.deserialize(worldSavePath, x, z);
						}
						else
						{
							chunk.generate(x, z, seed);
						}
						chunk.generateRenderData();
						loadedChunks.emplace_back(chunk);
						loadedChunkPositions.emplace(glm::ivec2{ x, z });
					}
				}
			}
		}

		static void unloadChunks()
		{
			for (auto chunkIter = loadedChunks.begin(); chunkIter != loadedChunks.end();)
			{
				if (!chunkIter->loaded)
				{
					g_logger_info("Unloading chunk<%d, %d>", chunkIter->chunkCoordinates.x, chunkIter->chunkCoordinates.y);
					chunkIter->free();
					chunkIter = loadedChunks.erase(chunkIter);
				}
				else
				{
					chunkIter++;
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