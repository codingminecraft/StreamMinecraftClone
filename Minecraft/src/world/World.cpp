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

namespace Minecraft
{
	namespace World
	{
		template<typename R>
		bool is_ready(std::future<R> const& f)
		{
			return f.wait_for(std::chrono::seconds(-10)) == std::future_status::ready;
		}

		// Internal declarations
		static void loadWorldTexture();
		static void checkChunkRadius();
		static void synchronizeChunks();
		static Chunk createChunk(int x, int z);
		static Chunk deleteChunk(Chunk chunk);

		// Members
		static RenderState renderState;
		static Camera camera;
		static PlayerController playerController;
		static Shader shader;
		static Texture worldTexture;
		static std::unordered_set<glm::ivec2> loadedChunkPositions;
		static const uint16 chunkRadius = 8;
		// Area of circle is PI * r^2, we'll round PI up to 4
		static const uint16 chunkCapacity = (chunkRadius + 1) * (chunkRadius + 1) * 4;
		static Chunk chunks[chunkCapacity];
		static const std::string worldSavePath = "world";
		static std::thread workerThreads[16];

		static std::mutex mutex;
		//static std::condition_variable cvar;
		static bool chunkNeedsWork = false;
		static bool threadRunning = true;

		static std::queue<std::future<Chunk>> queuedChunksForCreation;
		static std::queue<std::future<Chunk>> queuedChunksForDeletion;

		static int32 seed = 0;

		void init()
		{
			queuedChunksForCreation = {};
			queuedChunksForDeletion = {};

			srand((unsigned long)time(NULL));
			seed = (int32)(((float)rand() / (float)RAND_MAX) * 1000.0f);
			g_logger_info("World seed: %d", seed);

			// Initialize blocks
			//TexturePacker::packTextures("C:/dev/C++/MinecraftClone/assets/images/block", "textureFormat.yaml");
			g_memory_zeroMem(chunks, sizeof(Chunk) * chunkCapacity);

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
			for (int i = 0; i < chunkCapacity; i++)
			{
				Chunk& chunk = chunks[i];
				if (chunk.loaded)
				{
					NShader::uploadIVec2(shader, "uChunkPos", chunk.chunkCoordinates);
					chunk.render();

					const glm::ivec2 localChunkPos = chunk.chunkCoordinates - playerPositionInChunkCoords;
					bool hasBeenQueued = loadedChunkPositions.find(chunk.chunkCoordinates) == loadedChunkPositions.end();
					if (!hasBeenQueued && (localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) >= chunkRadius * chunkRadius)
					{
						queuedChunksForDeletion.push(std::async(std::launch::async, deleteChunk, chunk));
						loadedChunkPositions.erase(chunk.chunkCoordinates);
					}
				}
			}

			checkChunkRadius();
			synchronizeChunks();
		}

		void cleanup()
		{
			for (int i = 0; i < chunkCapacity; i++)
			{
				Chunk& chunk = chunks[i];
				chunk.freeCpu();
				chunk.freeGpu();
			}

			g_memory_free(chunks);
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
				if ((localPos.x * localPos.x) + (localPos.y * localPos.y) < chunkRadius * chunkRadius)
				{
					auto iter = loadedChunkPositions.find(position);
					if (iter == loadedChunkPositions.end())
					{
						queuedChunksForCreation.push(std::async(std::launch::async, createChunk, position.x, position.y));
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

					if (distance > chunkRadius * 2)
					{
						break;
					}
				}
			}
		}

		static void synchronizeChunks()
		{
			while (queuedChunksForDeletion.size() > 0 && is_ready(queuedChunksForDeletion.front()))
			{
				std::future<Chunk>& queuedChunk = queuedChunksForDeletion.front();
				Chunk chunk = queuedChunk.get();
				g_logger_info("Unloading from GPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
				chunk.freeGpu();
				queuedChunksForDeletion.pop();

				for (int i = 0; i < chunkCapacity; i++)
				{
					if (chunks[i].chunkCoordinates == chunk.chunkCoordinates)
					{
						chunks[i] = chunk;
						break;
					}
				}
			}

			while (queuedChunksForCreation.size() > 0 && is_ready(queuedChunksForCreation.front()))
			{
				std::future<Chunk>& queuedChunk = queuedChunksForCreation.front();
				Chunk chunk = queuedChunk.get();
				g_logger_info("Uploading to GPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
				chunk.uploadToGPU();
				queuedChunksForCreation.pop();

				for (int i = 0; i < chunkCapacity; i++)
				{
					if (!chunks[i].loaded)
					{
						chunks[i] = chunk;
						break;
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

		static Chunk createChunk(int x, int z)
		{
			Chunk chunk;
			chunk.chunkCoordinates = { x, z };
			g_logger_info("Loading on CPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
			if (Chunk::exists(worldSavePath, chunk.chunkCoordinates.x, chunk.chunkCoordinates.y))
			{
				chunk.deserialize(worldSavePath, chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
			}
			else
			{
				chunk.generate(chunk.chunkCoordinates.x, chunk.chunkCoordinates.y, seed);
			}
			chunk.generateRenderData();

			return chunk;
		}

		static Chunk deleteChunk(Chunk chunk)
		{
			g_logger_info("Unloading on CPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
			chunk.freeCpu();
			return chunk;
		}
	}
}