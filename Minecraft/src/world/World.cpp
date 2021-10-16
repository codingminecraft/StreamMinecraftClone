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
#include "core/Scene.h"
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
#include "gui/Gui.h"

namespace Minecraft
{
	namespace World
	{
		extern std::string worldSavePath = "world";

		// Internal declarations
		static void checkChunkRadius();

		// Members
		static Shader shader;
		static Texture worldTexture;
		static Ecs::EntityId playerId;
		static Ecs::EntityId randomEntity;
		static std::unordered_set<glm::ivec2> loadedChunkPositions;
		static Ecs::Registry* registry;

		static int32 seed;

		void init(Ecs::Registry& sceneRegistry)
		{
			// Initialize memory
			registry = &sceneRegistry;
			File::createDirIfNotExists(worldSavePath.c_str());

			// Generate a seed if needed
			srand((unsigned long)time(NULL));
			seed = (int32)(((float)rand() / (float)RAND_MAX) * 1000.0f);
			g_logger_info("World seed: %d", seed);

			// Initialize blocks
			TexturePacker::packTextures("assets/images/block", "assets/custom/textureFormat.yaml", "assets/custom/test.png");
			BlockMap::loadBlocks("assets/custom/textureFormat.yaml", "assets/custom/blockFormats.yaml");
			BlockMap::uploadTextureCoordinateMapToGpu();

			shader.compile("assets/shaders/default.glsl");
			worldTexture = TextureBuilder()
				.setFormat(ByteFormat::RGBA8_UI)
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setFilepath("assets/custom/test.png")
				.generate(true);

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
			transform.position.y = 289;
			transform.position.x = -145.0f;
			transform.position.z = 55.0f;
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
			transform2.position.x = -145.0f;
			transform2.position.z = 55.0f;
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
			PlayerController::init();
		}

		void free()
		{
			ChunkManager::free();
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
			Camera& camera = Scene::getCamera();
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
			ChunkManager::render(playerPosition, playerPositionInChunkCoords, shader);

			checkChunkRadius();
		}

		glm::ivec2 toChunkCoords(const glm::vec3& worldCoordinates)
		{
			return {
				glm::floor(worldCoordinates.x / 16.0f),
				glm::floor(worldCoordinates.z / 16.0f)
			};
		}

		static void checkChunkRadius()
		{
			const glm::vec3 playerPosition = registry->getComponent<Transform>(playerId).position;

			// Position changes as we circle out
			// See switch statement below
			glm::ivec2 position = toChunkCoords(playerPosition);
			glm::ivec2 playerPosChunkCoords = position;
			static glm::ivec2 lastPlayerPosChunkCoords = playerPosChunkCoords;

			// If the player moves to a new chunk, then retesselate any chunks on the edge of the chunk radius to 
			// fix any holes there might be
			bool retesselateEdges = false;
			if (lastPlayerPosChunkCoords != playerPosChunkCoords)
			{
				retesselateEdges = true;
			}

			bool needsWork = false;
			// 0 RIGHT, 1 UP, 2 LEFT, 3 DOWN
			uint8 direction = 0;
			uint8 distance = 1;
			uint8 timeToChangeDistance = 0;
			uint8 travelled = 0;
			while (true)
			{
				glm::ivec2 localPos = playerPosChunkCoords - position;
				if ((localPos.x * localPos.x) + (localPos.y * localPos.y) <= (World::ChunkRadius * World::ChunkRadius))
				{
					// We have to expand in a circle that exceeds the range of chunks in this radius,
					// so we also have to make sure that we check if the chunk is in range before we
					// try to queue it. Otherwise, we end up with infinite queues that instantly get deleted
					// which clog our threads with empty work.
					glm::ivec2 lastLocalPos = lastPlayerPosChunkCoords - position;
					bool retesselateThisChunk = 
						(lastLocalPos.x * lastLocalPos.x) + (lastLocalPos.y * lastLocalPos.y) >= ((World::ChunkRadius - 1) * (World::ChunkRadius - 1))
						&& retesselateEdges;
					ChunkManager::queueCreateChunk(position, retesselateThisChunk);
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

			lastPlayerPosChunkCoords = playerPosChunkCoords;
			ChunkManager::processCommands();
		}
	}
}