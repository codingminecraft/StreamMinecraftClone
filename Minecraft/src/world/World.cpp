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
#include "utils/TexturePacker.h"
#include "utils/DebugStats.h"
#include "gui/Gui.h"
#include "gui/MainHud.h"

namespace Minecraft
{
	namespace World
	{
		extern std::string savePath = "";
		extern uint32 seed = UINT32_MAX;
		extern std::atomic<float> seedAsFloat = 0.0f;
		extern std::string chunkSavePath = "";

		// Members
		static Shader shader;
		static Shader cubemapShader;
		static Texture worldTexture;
		static Cubemap skybox;
		static Ecs::EntityId playerId;
		static Ecs::EntityId randomEntity;
		static std::unordered_set<glm::ivec2> loadedChunkPositions;
		static Ecs::Registry* registry;
		static Frustum cameraFrustum;

		void init(Ecs::Registry& sceneRegistry)
		{
			// Initialize memory
			registry = &sceneRegistry;
			g_logger_assert(savePath != "", "World save path must not be empty.");

			// Initialize and create any filepaths for save information
			savePath = (std::filesystem::path(AppData::worldsRootPath) / std::filesystem::path(savePath)).string();
			File::createDirIfNotExists(savePath.c_str());
			chunkSavePath = (savePath / std::filesystem::path("chunks")).string();
			g_logger_info("World save folder at: %s", savePath.c_str());
			File::createDirIfNotExists(chunkSavePath.c_str());

			// Generate a seed if needed
			srand((unsigned long)time(NULL));
			if (File::isFile(getWorldDataFilepath(savePath).c_str()))
			{
				if (!deserialize())
				{
					g_logger_error("Could not load world. World.bin has been corrupted or does not exist.");
					return;
				}
			}

			if (seed == UINT32_MAX)
			{
				// Generate a seed between -INT32_MAX and INT32_MAX
				seed = (uint32)(((float)rand() / (float)RAND_MAX) * UINT32_MAX);
			}
			seedAsFloat = (float)((double)seed / (double)UINT32_MAX) * 2.0f - 1.0f;
			srand(seed);
			g_logger_info("World seed: %u", seed);
			g_logger_info("World seed (as float): %2.8f", seedAsFloat.load());

			// Initialize blocks
			const char* packedTexturesFilepath = "assets/custom/packedTextures.png";
			TexturePacker::packTextures("assets/images/block", "assets/custom/textureFormat.yaml", packedTexturesFilepath);
			BlockMap::loadBlocks("assets/custom/textureFormat.yaml", "assets/custom/blockFormats.yaml");
			BlockMap::uploadTextureCoordinateMapToGpu();

			shader.compile("assets/shaders/default.glsl");
			worldTexture = TextureBuilder()
				.setFormat(ByteFormat::RGBA8_UI)
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setFilepath(packedTexturesFilepath)
				.generateTextureObject()
				.bindTextureObject()
				.generate(true);

			cubemapShader.compile("assets/shaders/Cubemap.glsl");
			skybox = Cubemap::generateCubemap(
				"assets/images/sky/dayTop.jpg",
				"assets/images/sky/dayBottom.jpg",
				"assets/images/sky/dayLeft.jpg",
				"assets/images/sky/dayRight.jpg",
				"assets/images/sky/dayFront.jpg",
				"assets/images/sky/dayBack.jpg");

			// Setup player
			Ecs::EntityId player = registry->createEntity();
			playerId = player;
			registry->addComponent<Transform>(player);
			registry->addComponent<CharacterController>(player);
			registry->addComponent<BoxCollider>(player);
			registry->addComponent<Rigidbody>(player);
			registry->addComponent<Tag>(player);
			BoxCollider& boxCollider = registry->getComponent<BoxCollider>(player);
			boxCollider.size.x = 0.55f;
			boxCollider.size.y = 1.8f;
			boxCollider.size.z = 0.55f;
			Transform& playerTransform = registry->getComponent<Transform>(player);
			playerTransform.position.y = 289;
			playerTransform.position.x = -145.0f;
			playerTransform.position.z = 55.0f;
			CharacterController& controller = registry->getComponent<CharacterController>(player);
			controller.lockedToCamera = true;
			controller.controllerBaseSpeed = 1.8f;
			controller.controllerRunSpeed = 2.4f;
			controller.movementSensitivity = 0.6f;
			controller.isRunning = false;
			controller.movementAxis = glm::vec3();
			controller.viewAxis = glm::vec2();
			controller.applyJumpForce = false;
			controller.jumpForce = 4.7f;
			controller.downJumpForce = -10.2f;
			controller.cameraOffset = glm::vec3(0, 0.65f, 0);
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
			boxCollider2.size.x = 0.55f;
			boxCollider2.size.y = 1.8f;
			boxCollider2.size.z = 0.55f;
			Transform& transform2 = registry->getComponent<Transform>(randomEntity);
			transform2.position.y = 255;
			transform2.position.x = -145.0f;
			transform2.position.z = 55.0f;
			CharacterController& controller2 = registry->getComponent<CharacterController>(randomEntity);
			controller2.lockedToCamera = false;
			controller2.controllerBaseSpeed = 4.2f;
			controller2.controllerRunSpeed = 8.4f;
			controller2.isRunning = false;
			controller2.movementAxis = glm::vec3();
			controller2.viewAxis = glm::vec2();
			controller2.movementSensitivity = 0.6f;
			controller2.applyJumpForce = false;
			controller2.jumpForce = 16.0f;
			controller2.cameraOffset = glm::vec3(0, 0.65f, 0);
			Tag& tag2 = registry->getComponent<Tag>(randomEntity);
			tag2.type = TagType::None;

			ChunkManager::init();
			ChunkManager::checkChunkRadius(playerTransform.position);
			Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);
			PlayerController::init();
			MainHud::init();
		}

		void free()
		{
			serialize();
			ChunkManager::serialize();
			ChunkManager::free();
			MainHud::free();
		}

		void update(float dt)
		{
			Camera& camera = Scene::getCamera();
			glm::mat4 projectionMatrix = camera.calculateProjectionMatrix(*registry);
			glm::mat4 viewMatrix = camera.calculateViewMatrix(*registry);
			cameraFrustum.update(projectionMatrix * viewMatrix);
			Renderer::setCameraFrustum(cameraFrustum);

			// Render cubemap
			skybox.render(cubemapShader, projectionMatrix, viewMatrix);

			// Update all systems
			KeyHandler::update(dt);
			Physics::update(*registry, dt);
			CharacterSystem::update(*registry, dt);
			PlayerController::update(*registry, dt);
			// TODO: Figure out the best way to keep transform forward, right, up vectors correct
			TransformSystem::update(*registry, dt);
			MainHud::update(dt);

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
			shader.uploadMat4("uProjection", projectionMatrix);
			shader.uploadMat4("uView", viewMatrix);
			static int sunXRotation = 45;
			static int sunTicks = 0;
			sunTicks++;
			if (sunTicks > 20)
			{
				//sunXRotation++;
				sunTicks = 0;
			}
			glm::vec3 directionVector = glm::vec3(0.0f, glm::sin(glm::radians((float)sunXRotation)), glm::cos(glm::radians((float)sunXRotation)));
			shader.uploadVec3("uSunDirection", directionVector);
			shader.uploadBool("uIsDay", sunXRotation > 180 && sunXRotation < 360);
			sunXRotation = sunXRotation % 360;
			directionVector.y = sunXRotation > 180 && sunXRotation < 360 ? -directionVector.y : directionVector.y;
			glActiveTexture(GL_TEXTURE0);
			worldTexture.bind();
			shader.uploadInt("uTexture", 0);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_BUFFER, BlockMap::getTextureCoordinatesTextureId());
			shader.uploadInt("uTexCoordTexture", 1);

			// Render all the loaded chunks
			const glm::vec3& playerPosition = registry->getComponent<Transform>(playerId).position;
			glm::ivec2 playerPositionInChunkCoords = toChunkCoords(playerPosition);
			ChunkManager::render(playerPosition, playerPositionInChunkCoords, shader, cameraFrustum);

			// Check chunk radius if needed
			static glm::vec3 lastPlayerLoadPosition = playerPosition;
			if (glm::distance2(playerPosition, lastPlayerLoadPosition) > World::ChunkWidth * World::ChunkDepth)
			{
				lastPlayerLoadPosition = playerPosition;
				ChunkManager::checkChunkRadius(playerPosition);
			}
		}

		void serialize()
		{
			std::string filepath = getWorldDataFilepath(savePath);
			FILE* fp = fopen(filepath.c_str(), "wb");
			if (!fp)
			{
				g_logger_error("Could not serialize file '%s'", filepath.c_str());
				return;
			}

			// Write data
			fwrite(&seed, sizeof(uint32), 1, fp);
			fclose(fp);
		}

		bool deserialize()
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
			fclose(fp);

			return true;
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
	}
}