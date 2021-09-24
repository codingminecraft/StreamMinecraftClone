#include "core.h"
#include "world/World.h"
#include "world/ChunkManager.h"
#include "world/BlockMap.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Camera.h"
#include "renderer/Font.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "input/Input.h"
#include "input/KeyHandler.h"
#include "core/Application.h"
#include "core/File.h"
#include "core/Ecs.h"
#include "core/Components.h"
#include "core/TransformSystem.h"
#include "core/Window.h"
#include "physics/Physics.h"
#include "physics/PhysicsComponents.h"
#include "gameplay/PlayerController.h"
#include "gameplay/CharacterController.h"
#include "gameplay/CharacterSystem.h"
#include "utils/TexturePacker.h"
#include "utils/DebugStats.h"

namespace Minecraft
{
	namespace World
	{
		// Internal declarations
		static void checkChunkRadius();
		static void synchronizeChunks();
		static Chunk* findChunk(const glm::ivec2& chunkCoords);
		static void setUnloadedChunk(const Chunk& chunkVal);
		static void setChunk(const glm::ivec2& chunkCoords, const Chunk& chunkVal);

		// Members
		static Camera camera;
		static Shader shader;
		static Texture worldTexture;
		static Ecs::EntityId playerId;
		static Ecs::EntityId randomEntity;
		static std::unordered_set<glm::ivec2> loadedChunkPositions;
		static Chunk chunks[World::ChunkCapacity];
		static std::string worldSavePath;
		static Ecs::Registry* registry;

		static int32 seed;

		void init(Ecs::Registry& sceneRegistry)
		{
			// Initialize memory
			registry = &sceneRegistry;
			worldSavePath = "world";
			File::createDirIfNotExists(worldSavePath.c_str());
			g_memory_zeroMem(chunks, sizeof(Chunk) * ChunkCapacity);

			// Generate a seed if needed
			srand((unsigned long)time(NULL));
			seed = (int32)(((float)rand() / (float)RAND_MAX) * 1000.0f);
			g_logger_info("World seed: %d", seed);

			// Initialize blocks
			TexturePacker::packTextures("assets/images/block", "assets/blockFormat/textureFormat.yaml", "assets/blockFormat/test.png");
			BlockMap::loadBlocks("assets/blockFormat/textureFormat.yaml", "assets/blockFormat/blockFormats.yaml");
			BlockMap::uploadTextureCoordinateMapToGpu();

			shader.compile("assets/shaders/default.glsl");
			worldTexture = TextureBuilder()
				.setFormat(ByteFormat::RGBA8_UI)
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setFilepath("assets/blockFormat/test.png")
				.generate(true);

			// Setup camera
			Ecs::EntityId cameraEntity = registry->createEntity();
			registry->addComponent<Transform>(cameraEntity);
			registry->addComponent<Tag>(cameraEntity);
			Transform& cameraTransform = registry->getComponent<Transform>(cameraEntity);
			cameraTransform.position = glm::vec3(0, 257.0f, 1.0f);
			cameraTransform.orientation = glm::vec3(0.0f, 0.0f, 0.0f);
			camera.fov = 45.0f;
			camera.cameraEntity = cameraEntity;
			Tag& cameraTag = registry->getComponent<Tag>(cameraEntity);
			cameraTag.type = TagType::Camera;

			Renderer::setCamera(camera);

			// Setup player
			Ecs::EntityId player = registry->createEntity();
			playerId = player;
			registry->addComponent<Transform>(player);
			registry->addComponent<CharacterController>(player);
			registry->addComponent<BoxCollider>(player);
			registry->addComponent<Rigidbody>(player);
			registry->addComponent<Tag>(player);
			BoxCollider& boxCollider = registry->getComponent<BoxCollider>(player);
			boxCollider.size.x = 0.5f;
			boxCollider.size.y = 3.0f;
			boxCollider.size.z = 0.75f;
			Transform& transform = registry->getComponent<Transform>(player);
			transform.position.y = 255;
			transform.position.x = 45.0f;
			transform.position.z = -45.0f;
			CharacterController& controller = registry->getComponent<CharacterController>(player);
			controller.lockedToCamera = true;
			controller.controllerBaseSpeed = 4.0f;
			controller.controllerRunSpeed = 7.0f;
			controller.movementSensitivity = 0.1f;
			controller.isRunning = false;
			controller.movementAxis = glm::vec3();
			controller.viewAxis = glm::vec2();
			controller.applyJumpForce = false;
			controller.jumpForce = 8.0f;
			Tag& tag = registry->getComponent<Tag>(player);
			tag.type = TagType::Player;

			// Setup random physics entity
			randomEntity = registry->createEntity();
			registry->addComponent<Transform>(randomEntity);
			registry->addComponent<BoxCollider>(randomEntity);
			registry->addComponent<Rigidbody>(randomEntity);
			registry->addComponent<CharacterController>(randomEntity);
			registry->addComponent<Tag>(randomEntity);
			BoxCollider& boxCollider2 = registry->getComponent<BoxCollider>(randomEntity);
			boxCollider2.size.x = 0.5f;
			boxCollider2.size.y = 3.0f;
			boxCollider2.size.z = 0.75f;
			Transform& transform2 = registry->getComponent<Transform>(randomEntity);
			transform2.position.y = 255;
			transform2.position.x = 45.0f;
			transform2.position.z = -45.0f;
			CharacterController& controller2 = registry->getComponent<CharacterController>(randomEntity);
			controller2.lockedToCamera = false;
			controller2.controllerBaseSpeed = 4.2f;
			controller2.controllerRunSpeed = 8.4f;
			controller2.isRunning = false;
			controller2.movementAxis = glm::vec3();
			controller2.viewAxis = glm::vec2();
			controller2.movementSensitivity = 0.1f;
			controller2.applyJumpForce = false;
			controller2.jumpForce = 16.0f;
			Tag& tag2 = registry->getComponent<Tag>(randomEntity);
			tag2.type = TagType::None;

			ChunkManager::init(seed);
			checkChunkRadius();
			Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);
		}

		void free()
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

			registry->free();
		}

		void update(float dt)
		{
			// Update all systems
			KeyHandler::update(dt);
			Physics::update(*registry, dt);
			CharacterSystem::update(*registry, dt);
			PlayerController::update(*registry, dt);
			// TODO: Figure out the best way to keep transform forward, right, up vectors correct
			TransformSystem::update(*registry, dt);

			DebugStats::numDrawCalls = 0;
			static uint32 ticks = 0;
			ticks++;
			if (ticks > 10)
			{
				DebugStats::lastFrameTime = dt;
				ticks = 0;
			}

			// TODO: Remove me, I'm just here for testing purposes
			if (Input::keyBeginPress(GLFW_KEY_F5))
			{
				CharacterController& c1 = registry->getComponent<CharacterController>(playerId);
				CharacterController& c2 = registry->getComponent<CharacterController>(randomEntity);
				c1.lockedToCamera = !c1.lockedToCamera;
				c2.lockedToCamera = !c2.lockedToCamera;
				Tag& t1 = registry->getComponent<Tag>(playerId);
				Tag& t2 = registry->getComponent<Tag>(randomEntity);
				t1.type = c1.lockedToCamera ? TagType::Player : TagType::None;
				t2.type = c2.lockedToCamera ? TagType::Player : TagType::None;
			}

			// Upload shader variables
			shader.bind();
			shader.uploadMat4("uProjection", camera.calculateProjectionMatrix(*registry));
			shader.uploadMat4("uView", camera.calculateViewMatrix(*registry));
			shader.uploadVec3("uSunPosition", glm::vec3{ 1, 355, 1 });

			glActiveTexture(GL_TEXTURE0);
			worldTexture.bind();
			shader.uploadInt("uTexture", 0);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_BUFFER, BlockMap::getTextureCoordinatesTextureId());
			shader.uploadInt("uTexCoordTexture", 1);

			// Render all the loaded chunks
			const glm::vec3& playerPosition = registry->getComponent<Transform>(playerId).position;
			glm::ivec2 playerPositionInChunkCoords = toChunkCoords(playerPosition);
			for (int i = 0; i < ChunkCapacity; i++)
			{
				Chunk& chunk = chunks[i];
				if (chunk.loaded)
				{
					shader.uploadIVec2("uChunkPos", chunk.chunkCoordinates);
					shader.uploadVec3("uPlayerPosition", playerPosition);
					shader.uploadInt("uChunkRadius", World::ChunkRadius);
					chunk.render();

					const glm::ivec2 localChunkPos = chunk.chunkCoordinates - playerPositionInChunkCoords;
					bool hasBeenQueued = loadedChunkPositions.find(chunk.chunkCoordinates) == loadedChunkPositions.end();
					if (!hasBeenQueued && (localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) >= ChunkRadius * ChunkRadius)
					{
						ChunkManager::queueDeleteChunk(chunk);
						loadedChunkPositions.erase(chunk.chunkCoordinates);
					}

					DebugStats::numDrawCalls++;
				}
			}

			// Do line rendering type stuff
			Renderer::render();

			checkChunkRadius();
			synchronizeChunks();
		}

		glm::ivec2 toChunkCoords(const glm::vec3& worldCoordinates)
		{
			return {
				glm::floor(worldCoordinates.x / 16.0f),
				glm::floor(worldCoordinates.z / 16.0f)
			};
		}

		Chunk& getChunk(const glm::vec3& worldPosition)
		{
			glm::ivec2 chunkPosition = toChunkCoords(worldPosition);
			for (Chunk& chunk : chunks)
			{
				if (chunk.chunkCoordinates == chunkPosition)
				{
					return chunk;
				}
			}

			static Chunk defaultChunk = {};
			return defaultChunk;
		}

		Block getBlock(const glm::vec3& worldPosition)
		{
			Chunk& chunk = getChunk(worldPosition);
			if (chunk.chunkData)
			{
				glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunk.chunkCoordinates.x * 16.0f, 0.0f, chunk.chunkCoordinates.y * 16.0f));
				return chunk.getLocalBlock(localPosition);
			}
			return BlockMap::NULL_BLOCK;
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
			const glm::vec3 playerPosition = registry->getComponent<Transform>(playerId).position;

			// Position changes as we circle out
			// See switch statement below
			glm::ivec2 position = toChunkCoords(playerPosition);
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
						setChunk(chunk.chunkCoordinates, chunk);
					}
					else
					{
						g_logger_info("Uploading to GPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
						chunk.uploadToGPU();
						setUnloadedChunk(chunk);
					}
				}
			}
		}

		static Chunk* findChunk(const glm::ivec2& chunkCoords)
		{
			for (int i = 0; i < ChunkCapacity; i++)
			{
				if (chunks[i].chunkCoordinates == chunkCoords)
				{
					return &chunks[i];
				}
			}

			return nullptr;
		}

		static void setUnloadedChunk(const Chunk& chunkVal)
		{
			for (int i = 0; i < ChunkCapacity; i++)
			{
				if (!chunks[i].loaded)
				{
					chunks[i] = chunkVal;
					return;
				}
			}

			g_logger_warning("Tried to set unloaded chunk, but there are no unloaded chunks left.");
		}

		static void setChunk(const glm::ivec2& chunkCoords, const Chunk& chunkVal)
		{
			for (int i = 0; i < ChunkCapacity; i++)
			{
				if (chunks[i].chunkCoordinates == chunkCoords)
				{
					chunks[i] = chunkVal;
					return;
				}
			}

			g_logger_warning("Tried to set chunk<%d, %d> but could not find matching chunk.", chunkCoords.x, chunkCoords.y);
		}
	}
}