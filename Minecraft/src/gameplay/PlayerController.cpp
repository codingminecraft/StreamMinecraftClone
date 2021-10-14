#include "gameplay/PlayerController.h"
#include "input/Input.h"
#include "core/Ecs.h"
#include "core/Components.h"
#include "renderer/Camera.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "utils/DebugStats.h"
#include "physics/PhysicsComponents.h"
#include "physics/Physics.h"
#include "gameplay/CharacterController.h"
#include "world/ChunkManager.h"
#include "world/BlockMap.h"

namespace Minecraft
{
	enum class GameMode : uint8 
	{
		None,
		Adventure,
		Survival,
		Creative,
		Spectator
	};

	namespace PlayerController
	{
		// Internal members
		static Ecs::EntityId playerId;
		static Style blockHighlight;
		static GameMode gameMode;
		static float blockPlaceDebounceTime = 0.2f;
		static float blockPlaceDebounce = 0.0f;

		// Internal functions
		static void updateSurvival(float dt, Transform& transform, CharacterController& controller, Rigidbody& rb);
		static void updateSpectator(float dt, Transform& transform, CharacterController& controller, Rigidbody& rb);

		void init()
		{
			blockHighlight = Styles::defaultStyle;
			blockHighlight.color = "#00000067"_hex;
			blockHighlight.strokeWidth = 0.02f;
			gameMode = GameMode::Survival;
			playerId = Ecs::nullEntity;
		}

		void update(Ecs::Registry& registry, float dt)
		{
			if (playerId == Ecs::nullEntity || registry.getComponent<Tag>(playerId).type != TagType::Player)
			{
				playerId = registry.find(TagType::Player);
			}

			if (playerId != Ecs::nullEntity && registry.hasComponent<Transform>(playerId) && registry.hasComponent<CharacterController>(playerId)
				&& registry.hasComponent<Rigidbody>(playerId))
			{
				Transform& transform = registry.getComponent<Transform>(playerId);
				CharacterController& controller = registry.getComponent<CharacterController>(playerId);
				Rigidbody& rb = registry.getComponent<Rigidbody>(playerId);

				switch (gameMode)
				{
				case GameMode::Survival:
					updateSurvival(dt, transform, controller, rb);
					break;
				case GameMode::Spectator:
					updateSpectator(dt, transform, controller, rb);
					break;
				default:
					break;
				}

				DebugStats::playerPos = transform.position;
				DebugStats::playerOrientation = transform.orientation;
			}
		}

		static void updateSurvival(float dt, Transform& transform, CharacterController& controller, Rigidbody& rb)
		{
			blockPlaceDebounce -= dt;

			RaycastStaticResult res = Physics::raycastStatic(transform.position, transform.forward, 5.0f);
			if (res.hit)
			{
				Renderer::drawBox(res.blockCenter, res.blockSize, blockHighlight);
				Renderer::drawBox(res.point, glm::vec3(0.1f, 0.1f, 0.1f), Styles::defaultStyle);
				
				if (Input::isMousePressed(GLFW_MOUSE_BUTTON_RIGHT) && blockPlaceDebounce <= 0)
				{
					glm::vec3 worldPos = res.point + (res.hitNormal * 0.1f);
					static Block newBlock {
						3, 0, 0, 0
					};
					ChunkManager::setBlock(worldPos, newBlock);
					blockPlaceDebounce = blockPlaceDebounceTime;
				}
				else if (Input::isMousePressed(GLFW_MOUSE_BUTTON_LEFT) && blockPlaceDebounce <= 0)
				{
					glm::vec3 worldPos = res.point - (res.hitNormal * 0.1f);
					static Block newBlock{
						3, 0, 0, 0
					};
					ChunkManager::removeBlock(worldPos);
					blockPlaceDebounce = blockPlaceDebounceTime;
				}
			}

			controller.viewAxis.x = Input::deltaMouseX;
			controller.viewAxis.y = Input::deltaMouseY;
			controller.isRunning = Input::isKeyPressed(GLFW_KEY_LEFT_CONTROL);

			controller.movementAxis.x =
				Input::isKeyPressed(GLFW_KEY_W)
				? 1.0f
				: Input::isKeyPressed(GLFW_KEY_S)
				? -1.0f
				: 0.0f;
			controller.movementAxis.z =
				Input::isKeyPressed(GLFW_KEY_D)
				? 1.0f
				: Input::isKeyPressed(GLFW_KEY_A)
				? -1.0f
				: 0.0f;

			if (rb.onGround)
			{
				if (Input::keyBeginPress(GLFW_KEY_SPACE))
				{
					controller.applyJumpForce = true;
				}
			}

			//Physics::raycastStatic(transform.position, transform.forward, 5.0f);

			if (Input::keyBeginPress(GLFW_KEY_F4))
			{
				gameMode = GameMode::Spectator;
				rb.isSensor = true;
			}
		}

		static void updateSpectator(float dt, Transform& transform, CharacterController& controller, Rigidbody& rb)
		{
			controller.viewAxis.x = Input::deltaMouseX;
			controller.viewAxis.y = Input::deltaMouseY;
			controller.isRunning = Input::isKeyPressed(GLFW_KEY_LEFT_CONTROL);

			controller.movementAxis.x =
				Input::isKeyPressed(GLFW_KEY_W)
				? 1.0f
				: Input::isKeyPressed(GLFW_KEY_S)
				? -1.0f
				: 0.0f;
			controller.movementAxis.y =
				Input::isKeyPressed(GLFW_KEY_SPACE)
				? 1.0f
				: Input::isKeyPressed(GLFW_KEY_LEFT_SHIFT)
				? -1.0f
				: 0.0f;
			controller.movementAxis.z =
				Input::isKeyPressed(GLFW_KEY_D)
				? 1.0f
				: Input::isKeyPressed(GLFW_KEY_A)
				? -1.0f
				: 0.0f;

			if (Input::keyBeginPress(GLFW_KEY_F4))
			{
				gameMode = GameMode::Survival;
				rb.isSensor = false;
			}
		}
	}
}