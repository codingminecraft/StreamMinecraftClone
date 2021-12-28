#include "core.h"
#include "world/World.h"
#include "world/ChunkManager.h"
#include "world/BlockMap.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Camera.h"
#include "renderer/Font.h"
#include "renderer/Cubemap.h"
#include "renderer/Renderer.h"
#include "renderer/Frustum.h"
#include "renderer/Styles.h"
#include "input/Input.h"
#include "input/KeyHandler.h"
#include "core/Application.h"
#include "core/File.h"
#include "core/Ecs.h"
#include "core/Scene.h"
#include "core/Components.h"
#include "core/TransformSystem.h"
#include "core/Window.h"
#include "core/AppData.h"
#include "physics/Physics.h"
#include "physics/PhysicsComponents.h"
#include "gameplay/PlayerController.h"
#include "gameplay/CharacterController.h"
#include "gameplay/CharacterSystem.h"
#include "gameplay/Inventory.h"
#include "utils/TexturePacker.h"
#include "utils/DebugStats.h"
#include "utils/CMath.h"
#include "gui/Gui.h"
#include "gui/MainHud.h"
#include "network/Network.h"
#include "gui/ChunkLoadingScreen.h"
#include "world/TerrainGenerator.h"

namespace Minecraft
{
	namespace World
	{
		std::string savePath = "";
		uint32 seed = UINT32_MAX;
		std::atomic<float> seedAsFloat = 0.0f;
		std::string chunkSavePath = "";
		int worldTime = 0;
		bool doDaylightCycle = false;
		float deltaTime = 0.0f;

		// Members
		static std::string lastSavePath;
		static std::string lastChunkSavePath;

		static Shader opaqueShader;
		static Shader transparentShader;
		static Shader cubemapShader;
		static Cubemap skybox;
		static Ecs::EntityId playerId;
		static robin_hood::unordered_set<glm::ivec2> loadedChunkPositions;
		static Ecs::Registry* registry;
		static glm::vec2 lastPlayerLoadPosition;
		static bool isClient;
		static bool isLoading;
		static std::thread asyncInitThread;

		// Tmp
		static const char* localPlayerName = "Local Player";

		// Internal functions
		static void asyncInit(glm::vec3 playerPosition, bool isClient);

		void init(Ecs::Registry& sceneRegistry, const char* hostname, int port)
		{
			isLoading = true;
			ChunkLoadingScreen::init();
			registry = &sceneRegistry;

			playerId = Ecs::nullEntity;

			isClient = false;

			// Initialize memory
			ChunkManager::init();

			lastPlayerLoadPosition = glm::vec2(-145.0f, 55.0f);

			if (strcmp(hostname, "") != 0 && port != 0)
			{
				isClient = true;
				Network::init(false, hostname, port);
				asyncInitThread = std::thread(asyncInit, glm::vec3(), isClient);
			}
			else
			{
				// Initialize and create any filepaths for save information
				g_logger_assert(savePath != "", "World save path must not be empty.");
				setSavePath(savePath);

				// Generate a seed if needed
				Transform* playerTransform = nullptr;
				srand((unsigned long)time(NULL));
				if (File::isFile(getWorldDataFilepath(savePath).c_str()))
				{
					if (!deserialize())
					{
						g_logger_error("Could not load world. World.bin has been corrupted or does not exist.");
						return;
					}

					for (auto entity : registry->view<PlayerComponent>())
					{
						const PlayerComponent& playerComponent = registry->getComponent<PlayerComponent>(entity);
						if (std::strcmp(playerComponent.name, localPlayerName) == 0)
						{
							playerId = entity;
							break;
						}
					}
					g_logger_assert(playerId != Ecs::nullEntity, "Failed to find a player '%s' from serialized world. Possible save corruption.", localPlayerName);
					playerTransform = &registry->getComponent<Transform>(playerId);
					PlayerComponent& playerComp = registry->getComponent<PlayerComponent>(playerId);
					g_logger_info("Deserialized player '%s'", playerComp.name);
				}
				else
				{
					// Setup player if this is a new world
					playerId = createPlayer(localPlayerName, glm::vec3(-145.0f, 289, 55.0f));
					playerTransform = &registry->getComponent<Transform>(playerId);
				}

				if (seed == UINT32_MAX)
				{
					// Generate a seed between -INT32_MAX and INT32_MAX
					seed = (uint32)(((float)rand() / (float)RAND_MAX) * UINT32_MAX);
				}
				seedAsFloat = (float)((double)seed / (double)UINT32_MAX) * 2.0f - 1.0f;
				srand(seed);
				g_logger_info("Loading world in single player mode locally.");
				g_logger_info("World seed: %u", seed);

				g_logger_assert(playerTransform != nullptr, "Failed to find player or create player when initializing world.");
				lastPlayerLoadPosition = glm::vec2(playerTransform->position.x, playerTransform->position.z);
				TerrainGenerator::init("assets/custom/terrainNoise.yaml", seed);
				asyncInitThread = std::thread(asyncInit, playerTransform->position, isClient);
			}

			reloadShaders();
			skybox = Cubemap::generateCubemap(
				"assets/images/sky/dayTop.png",
				"assets/images/sky/dayBottom.png",
				"assets/images/sky/dayLeft.png",
				"assets/images/sky/dayRight.png",
				"assets/images/sky/dayFront.png",
				"assets/images/sky/dayBack.png");

			Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);
			PlayerController::init();
			MainHud::init();
		}

		void reloadShaders()
		{
			opaqueShader.destroy();
			transparentShader.destroy();
			cubemapShader.destroy();
			opaqueShader.compile("assets/shaders/OpaqueShader.glsl");
			transparentShader.compile("assets/shaders/TransparentShader.glsl");
			cubemapShader.compile("assets/shaders/Cubemap.glsl");
		}

		void regenerateWorld()
		{
			// Free the chunks but don't serialize chunks
			if (asyncInitThread.joinable())
			{
				asyncInitThread.join();
			}

			TerrainGenerator::free();
			TerrainGenerator::init("assets/custom/terrainNoise.yaml", seed);

			ChunkManager::free();
			ChunkManager::init();
			isLoading = true;
			glm::vec3 playerPos = glm::vec3(lastPlayerLoadPosition.x, 128.0f, lastPlayerLoadPosition.y);
			asyncInitThread = std::thread(asyncInit, playerPos, isClient);
		}

		void setSavePath(const std::string& newSavePath) 
		{
			if (newSavePath == "") 
			{
				savePath = "";
				chunkSavePath = "";
				return;
			}

			savePath = (std::filesystem::path(AppData::worldsRootPath) / std::filesystem::path(newSavePath)).string();
			File::createDirIfNotExists(savePath.c_str());
			chunkSavePath = (savePath / std::filesystem::path("chunks")).string();
			g_logger_info("World save folder at: %s", savePath.c_str());
			File::createDirIfNotExists(chunkSavePath.c_str());
		}

		void pushSavePath(const std::string& newSavePath)
		{
			lastSavePath = savePath;
			lastChunkSavePath = chunkSavePath;

			setSavePath(newSavePath);
		}

		void popSavePath()
		{
			savePath = lastSavePath;
			chunkSavePath = lastChunkSavePath;
		}

		void free(bool shouldSerialize)
		{
			if (asyncInitThread.joinable())
			{
				asyncInitThread.join();
			}

			Application::takeScreenshot((savePath + "/worldIcon.png").c_str());

			// Force any connections that might have been opened to close
			Network::free();

			opaqueShader.destroy();
			transparentShader.destroy();
			skybox.destroy();
			cubemapShader.destroy();

			if (shouldSerialize) 
			{
				serialize();
				ChunkManager::serialize();
			}
			ChunkManager::free();
			MainHud::free();
			TerrainGenerator::free();
			ChunkLoadingScreen::free();

			registry->clear();

			seed = UINT32_MAX;
			seedAsFloat = FLT_MAX;
		}

		void update(Frustum& cameraFrustum, const Texture& worldTexture)
		{
#ifdef _USE_OPTICK
			OPTICK_EVENT();
#endif

			if (isLoading)
			{
				float percentLoaded = ChunkManager::percentWorkDone();
				if (percentLoaded == 1.0f)
				{
					asyncInitThread.join();
					isLoading = false;
					Application::getWindow().setCursorMode(CursorMode::Locked);
				}
				else
				{
					ChunkLoadingScreen::update(percentLoaded);
					return;
				}
			}

			// TODO: Find a better way to set up players than this. They
			// should be set at world initialization and not dynamically like this
			if (playerId == Ecs::nullEntity)
			{
				playerId = registry->find(TagType::Player);
				g_logger_warning("Uh oh. We had to resort to a baaaaddd method for finding the player.");
			}

			// TODO: Figure out the best way to keep transform forward, right, up vectors correct
			TransformSystem::update(*registry);
			// Draw cubemap and update camera
			Camera& camera = Scene::getCamera();
			glm::mat4 projectionMatrix = camera.calculateProjectionMatrix(*registry);
			glm::mat4 viewMatrix = camera.calculateViewMatrix(*registry);
			cameraFrustum.update(projectionMatrix * viewMatrix);

			// Render cubemap
			skybox.render(cubemapShader, projectionMatrix, viewMatrix);

			// Update all systems
			Network::update();
			KeyHandler::update();
			Physics::update(*registry);
			PlayerController::update(*registry);
			CharacterSystem::update(*registry);

			DebugStats::numDrawCalls = 0;
			static uint32 ticks = 0;
			ticks++;
			if (ticks > 10)
			{
				DebugStats::lastFrameTime = World::deltaTime;
				ticks = 0;
			}

			if (doDaylightCycle)
			{
				worldTime = (worldTime + 10) % 2400;
			}
			int sunXRotation;
			// Since 0-180 is daytime, but 600-1800 is daytime in actual units, we need to offset our time here
			// 120 degrees - 300 degrees is daytime
			if (worldTime >= 600 && worldTime <= 1800)
			{
				sunXRotation = (int)CMath::mapRange((float)worldTime, 600.0f, 1800.0f, -45.0f, 235.0f);
				if (sunXRotation < 0)
				{
					sunXRotation = 360 - sunXRotation;
				}
			}
			else if (worldTime > 1800)
			{
				sunXRotation = (int)CMath::mapRange((float)worldTime, 1800.0f, 2400.0f, 235.0f, 240.0f);
			}
			else
			{
				sunXRotation = (int)CMath::mapRange((float)worldTime, 0.0f, 600.0f, 240.0f, 315.0f);
			}

			// Upload Transparent Shader variables
			transparentShader.bind();
			transparentShader.uploadMat4("uProjection", projectionMatrix);
			transparentShader.uploadMat4("uView", viewMatrix);
			glm::vec3 directionVector = glm::normalize(glm::vec3(0.0f, glm::sin(glm::radians((float)sunXRotation)), glm::cos(glm::radians((float)sunXRotation))));
			transparentShader.uploadVec3("uSunDirection", directionVector);
			transparentShader.uploadBool("uIsDay", sunXRotation > 180 && sunXRotation < 360);
			glActiveTexture(GL_TEXTURE0);
			worldTexture.bind();
			transparentShader.uploadInt("uTexture", 0);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_BUFFER, BlockMap::getTextureCoordinatesTextureId());
			transparentShader.uploadInt("uTexCoordTexture", 1);

			// Upload Opaque Shader variables
			opaqueShader.bind();
			opaqueShader.uploadMat4("uProjection", projectionMatrix);
			opaqueShader.uploadMat4("uView", viewMatrix);
			opaqueShader.uploadVec3("uSunDirection", directionVector);
			opaqueShader.uploadBool("uIsDay", sunXRotation > 180 && sunXRotation < 360);

			glActiveTexture(GL_TEXTURE0);
			worldTexture.bind();
			opaqueShader.uploadInt("uTexture", 0);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_BUFFER, BlockMap::getTextureCoordinatesTextureId());
			opaqueShader.uploadInt("uTexCoordTexture", 1);

			// Render all the loaded chunks
			if (playerId != Ecs::nullEntity && registry->hasComponent<Transform>(playerId))
			{
				const glm::vec3& playerPosition = registry->getComponent<Transform>(playerId).position;
				glm::ivec2 playerPositionInChunkCoords = toChunkCoords(playerPosition);
				ChunkManager::render(playerPosition, playerPositionInChunkCoords, opaqueShader, transparentShader, cameraFrustum);

				// Check chunk radius if needed
				if (glm::distance2(glm::vec2(playerPosition.x, playerPosition.z), lastPlayerLoadPosition) > World::ChunkWidth * World::ChunkDepth)
				{
					lastPlayerLoadPosition = glm::vec2(playerPosition.x, playerPosition.z);
					ChunkManager::checkChunkRadius(playerPosition, isClient);
				}
			}
		}

		void givePlayerBlock(int blockId, int blockCount)
		{
			Inventory& inventory = registry->getComponent<Inventory>(playerId);
			for (int i = 0; i < Player::numHotbarSlots; i++)
			{
				if (!inventory.slots[i].blockId)
				{
					inventory.slots[i].blockId = blockId;
					inventory.slots[i].count = blockCount;
					break;
				}
			}
		}

		bool isPlayerUnderwater()
		{
			if (playerId != Ecs::nullEntity && registry->hasComponent<Transform>(playerId) &&
				registry->hasComponent<CharacterController>(playerId))
			{
				Transform& transform = registry->getComponent<Transform>(playerId);
				CharacterController& characterController = registry->getComponent<CharacterController>(playerId);
				Block blockAtEyeLevel = ChunkManager::getBlock(transform.position + characterController.cameraOffset);
				return blockAtEyeLevel.id == 19;
			}
			return false;
		}

		bool isLoaded()
		{
			return !isLoading;
		}

		Ecs::EntityId getLocalPlayer()
		{
			return playerId;
		}

		void setLocalPlayer(Ecs::EntityId localPlayer)
		{
			playerId = localPlayer;
		}

		Ecs::EntityId createPlayer(const char* playerName, const glm::vec3& position)
		{
			Ecs::EntityId player = registry->createEntity();
			registry->addComponent<Transform>(player);
			registry->addComponent<CharacterController>(player);
			registry->addComponent<BoxCollider>(player);
			registry->addComponent<Rigidbody>(player);
			registry->addComponent<Tag>(player);
			registry->addComponent<Inventory>(player);
			registry->addComponent<PlayerComponent>(player);
			BoxCollider& boxCollider = registry->getComponent<BoxCollider>(player);
			boxCollider.size.x = 0.55f;
			boxCollider.size.y = 1.8f;
			boxCollider.size.z = 0.55f;
			Transform& transform = registry->getComponent<Transform>(player);
			transform.position.x = position.x;
			transform.position.y = position.y;
			transform.position.z = position.z;
			CharacterController& controller = registry->getComponent<CharacterController>(player);
			controller.lockedToCamera = true;
			controller.controllerBaseSpeed = 4.4f;
			controller.controllerRunSpeed = 6.2f;
			controller.movementSensitivity = 0.6f;
			controller.isRunning = false;
			controller.movementAxis = glm::vec3();
			controller.viewAxis = glm::vec2();
			controller.applyJumpForce = false;
			controller.jumpForce = 7.6f;
			controller.downJumpForce = -25.0f;
			controller.cameraOffset = glm::vec3(0, 0.65f, 0);
			Inventory& inventory = registry->getComponent<Inventory>(player);
			g_memory_zeroMem(&inventory, sizeof(Inventory));
			Tag& tag = registry->getComponent<Tag>(player);
			tag.type = TagType::Player;
			PlayerComponent& playerComp = registry->getComponent<PlayerComponent>(player);
			playerComp.setName(playerName);

			return player;
		}

		void serialize()
		{
			if (!isClient)
			{
				std::string filepath = getWorldDataFilepath(savePath);
				g_logger_info("Saving world to '%s'", filepath.c_str());
				FILE* fp = fopen(filepath.c_str(), "wb");
				if (!fp)
				{
					g_logger_error("Could not serialize file '%s'", filepath.c_str());
					return;
				}

				// Write data
				fwrite(&seed, sizeof(uint32), 1, fp);
				fwrite(&worldTime, sizeof(int), 1, fp);
				// TODO: This will fail for serialization/deserialization on 64->32 bit systems or vice versa
				RawMemory serializedRegistry = registry->serialize();
				fwrite(&serializedRegistry.size, sizeof(size_t), 1, fp);
				fwrite(serializedRegistry.data, serializedRegistry.size, 1, fp);
				g_memory_free(serializedRegistry.data);
				fclose(fp);
			}
			else
			{
				// TODO: If we are a client send our data to the server to be saved
			}
		}

		bool deserialize()
		{
			if (!isClient)
			{
				std::string filepath = getWorldDataFilepath(savePath);
				FILE* fp = fopen(filepath.c_str(), "rb");
				if (!fp)
				{
					g_logger_error("Could not open file '%s'", filepath.c_str());
					return false;
				}

				// Read data
				fread(&seed, sizeof(uint32), 1, fp);
				fread(&worldTime, sizeof(int), 1, fp);
				// TODO: This will fail for serialization/deserialization on 64->32 bit systems or vice versa
				RawMemory registryData = { 0, 0 };
				fread(&registryData.size, sizeof(size_t), 1, fp);
				uint8* tmpData = (uint8*)g_memory_allocate(registryData.size);
				fread(tmpData, registryData.size, 1, fp);
				registryData.data = tmpData;
				registry->deserialize(registryData);
				g_memory_free(tmpData);
				fclose(fp);

				return true;
			}
			else
			{
				// TODO: Get the world info from the server on connection
			}
			return false;
		}

		glm::ivec2 toChunkCoords(const glm::vec3& worldCoordinates)
		{
			return {
				glm::floor(worldCoordinates.x / 16.0f),
				glm::floor(worldCoordinates.z / 16.0f)
			};
		}

		std::string getWorldDataFilepath(const std::string& worldSavePath)
		{
			return worldSavePath + "/world.bin";
		}

		std::string getWorldReplayDirPath(const std::string& worldSavePath)
		{
			return worldSavePath + "/replay";
		}

		static void asyncInit(glm::vec3 playerPosition, bool isClient)
		{
			ChunkManager::checkChunkRadius(playerPosition, isClient);
		}
	}
}