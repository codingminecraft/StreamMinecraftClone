#include "gameplay/PlayerController.h"
#include "input/Input.h"
#include "core/Ecs.h"
#include "core/Components.h"
#include "renderer/Camera.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "renderer/Sprites.h"
#include "utils/DebugStats.h"
#include "utils/CMath.h"
#include "physics/PhysicsComponents.h"
#include "physics/Physics.h"
#include "gameplay/CharacterController.h"
#include "world/ChunkManager.h"
#include "world/BlockMap.h"
#include "gui/MainHud.h"

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

		static int hotbarBlockIds[9] = {2, 3, 4, 6, 8, 9, 10, 11, 2};

		static const TextureFormat* sideSprite;
		static const TextureFormat* topSprite;
		static const TextureFormat* bottomSprite;

		// Internal functions
		static void updateSurvival(float dt, Transform& transform, CharacterController& controller, Rigidbody& rb);
		static void updateSpectator(float dt, Transform& transform, CharacterController& controller, Rigidbody& rb);
		static void updateInventory(float dt);

		void init()
		{
			blockHighlight = Styles::defaultStyle;
			blockHighlight.color = "#00000067"_hex;
			blockHighlight.strokeWidth = 0.01f;
			gameMode = GameMode::Survival;
			playerId = Ecs::nullEntity;

			sideSprite = &BlockMap::getTextureFormat("grass_block_side");
			topSprite = &BlockMap::getTextureFormat("grass_block_top");
			bottomSprite = &BlockMap::getTextureFormat("dirt");
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

			RaycastStaticResult res = Physics::raycastStatic(transform.position + controller.cameraOffset, transform.forward, 5.0f);
			if (res.hit)
			{
				Renderer::drawBox(res.blockCenter, res.blockSize + glm::vec3(0.005f, 0.005f, 0.005f), blockHighlight);
				//Renderer::drawBox(res.point, glm::vec3(0.1f, 0.1f, 0.1f), Styles::defaultStyle);
				Renderer::drawTexturedCube(res.point + (res.hitNormal * 0.1f), glm::vec3(0.1f, 0.1f, 0.1f), *sideSprite, *topSprite, *bottomSprite);

				if (Input::isMousePressed(GLFW_MOUSE_BUTTON_RIGHT) && blockPlaceDebounce <= 0)
				{
					glm::vec3 worldPos = res.point + (res.hitNormal * 0.1f);
					static Block newBlock{
						0, 0, 0, 0
					};
					newBlock.id = hotbarBlockIds[MainHud::currentInventorySlot];
					ChunkManager::setBlock(worldPos, newBlock);
					blockPlaceDebounce = blockPlaceDebounceTime;
				}
				else if (Input::isMousePressed(GLFW_MOUSE_BUTTON_LEFT) && blockPlaceDebounce <= 0)
				{
					glm::vec3 worldPos = res.point - (res.hitNormal * 0.1f);
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

			updateInventory(dt);
			MainHud::update(dt);
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

		static void updateInventory(float dt)
		{
			for (int i = 0; i < 9; i++)
			{
				if (Input::keyBeginPress(GLFW_KEY_1 + i))
				{
					MainHud::currentInventorySlot = i;
				}
			}

			if (Input::mouseScrollY != 0)
			{
				MainHud::currentInventorySlot += (int)Input::mouseScrollY;
				if (MainHud::currentInventorySlot < 0)
				{
					MainHud::currentInventorySlot = CMath::negativeMod(MainHud::currentInventorySlot, 0, 8);
				}
				else if (MainHud::currentInventorySlot > 8)
				{
					MainHud::currentInventorySlot = MainHud::currentInventorySlot % 9;
				}
			}
		}
	}
}