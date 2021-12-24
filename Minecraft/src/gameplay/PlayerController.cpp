#include "gameplay/PlayerController.h"
#include "input/Input.h"
#include "input/KeyBindings.h"
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
#include "gameplay/Inventory.h"
#include "gameplay/CommandLine.h"
#include "world/ChunkManager.h"
#include "world/BlockMap.h"
#include "gui/MainHud.h"
#include "core/Application.h"
#include "core/Scene.h"
#include "core/Window.h"

#include "utils/Constants.h"

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

	enum class CubemapSide : uint8
	{
		Left = 0,
		Right = 1,
		Front = 2,
		Back = 3,
		Top = 4,
		Bottom = 5,
		Length
	};

	namespace PlayerController
	{
		bool generateCubemap = false;
		static CubemapSide sideGenerating = CubemapSide::Left;

		// Internal members
		static Ecs::EntityId playerId;
		static Style blockHighlight;
		static GameMode gameMode;
		static float blockPlaceDebounceTime = 0.2f;
		static float blockPlaceDebounce = 0.0f;

		static const TextureFormat* sideSprite;
		static const TextureFormat* topSprite;
		static const TextureFormat* bottomSprite;

		// Internal functions
		static void updateSurvival(float dt, Transform& transform, CharacterController& controller, Rigidbody& rb, Inventory& inventory);
		static void updateCreative(float dt, Transform& transform, CharacterController& controller, Rigidbody& rb, Inventory& inventory);
		static void updateSpectator(float dt, Transform& transform, CharacterController& controller, Rigidbody& rb);
		static void updateInventory(float dt, Inventory& inventory);

		static Model stick;

		void init()
		{
			blockHighlight = Styles::defaultStyle;
			blockHighlight.color = "#00000067"_hex;
			blockHighlight.strokeWidth = 0.01f;
			gameMode = GameMode::Survival;
			playerId = Ecs::nullEntity;

			sideSprite = &BlockMap::getTextureFormat("oak_log");
			topSprite = &BlockMap::getTextureFormat("oak_log_top");
			bottomSprite = &BlockMap::getTextureFormat("oak_log_top");

			stick = Vertices::getItemModel("stick");
		}

		void update(Ecs::Registry& registry, float dt)
		{
			if (playerId == Ecs::nullEntity || registry.getComponent<Tag>(playerId).type != TagType::Player)
			{
				playerId = registry.find(TagType::Player);
				if (gameMode == GameMode::Survival)
				{
					if (registry.hasComponent<CharacterController>(playerId) && registry.hasComponent<Rigidbody>(playerId))
					{
						CharacterController& controller = registry.getComponent<CharacterController>(playerId);
						Rigidbody& rb = registry.getComponent<Rigidbody>(playerId);
						controller.controllerBaseSpeed = 4.4f;
						controller.controllerRunSpeed = 6.2f;
						rb.useGravity = true;
					}
				}
			}

			if (playerId != Ecs::nullEntity && registry.hasComponent<Transform>(playerId) && registry.hasComponent<CharacterController>(playerId)
				&& registry.hasComponent<Rigidbody>(playerId) && registry.hasComponent<Inventory>(playerId))
			{
				Transform& transform = registry.getComponent<Transform>(playerId);
				CharacterController& controller = registry.getComponent<CharacterController>(playerId);
				Rigidbody& rb = registry.getComponent<Rigidbody>(playerId);
				Inventory& inventory = registry.getComponent<Inventory>(playerId);

				if (generateCubemap)
				{
					controller.movementAxis.x = 0;
					controller.movementAxis.y = 0;
					controller.movementAxis.z = 0;
					controller.applyJumpForce = false;
					controller.viewAxis.x = 0;
					controller.viewAxis.y = 0;
					Scene::getCamera().fov = 90.0f;

					switch (sideGenerating)
					{
					case CubemapSide::Back:
						transform.orientation.x = 0.0f;
						transform.orientation.y = 180.0f;
						break;
					case CubemapSide::Left:
						transform.orientation.x = 0.0f;
						transform.orientation.y = 90.0f;
						break;
					case CubemapSide::Front:
						transform.orientation.x = 0.0f;
						transform.orientation.y = 0.0f;
						break;
					case CubemapSide::Right:
						transform.orientation.x = 0.0f;
						transform.orientation.y = 270.0f;
						break;
					case CubemapSide::Top:
						transform.orientation.x = 89.9f;
						transform.orientation.y = 0.0f;
						break;
					case CubemapSide::Bottom:
						transform.orientation.x = -89.9f;
						transform.orientation.y = 0.0f;
						break;
					}

					Application::takeScreenshot(magic_enum::enum_name(sideGenerating).data(), true);
					sideGenerating = (CubemapSide)((int)sideGenerating + 1);
					if (sideGenerating == CubemapSide::Length)
					{
						sideGenerating = (CubemapSide)0;
						generateCubemap = false;
						Scene::getCamera().fov = 45.0f;
					}
					// Don't update the HUD or anything
					return;
				}

				switch (gameMode)
				{
				case GameMode::Survival:
					updateSurvival(dt, transform, controller, rb, inventory);
					break;
				case GameMode::Creative:
					updateCreative(dt, transform, controller, rb, inventory);
					break;
				case GameMode::Spectator:
					updateSpectator(dt, transform, controller, rb);
					break;
				default:
					break;
				}

				// Common key bindings across all game modes
				if (KeyBindings::keyBeginPress(KeyBind::Escape))
				{
					if (CommandLine::isActive)
					{
						CommandLine::isActive = false;
					}
					else if (MainHud::viewingCraftScreen)
					{
						MainHud::viewingCraftScreen = false;
					}
					else if (!MainHud::isPaused)
					{
						Application::getWindow().setCursorMode(CursorMode::Normal);
						MainHud::isPaused = true;
					}
					else if (MainHud::isPaused)
					{
						Application::getWindow().setCursorMode(CursorMode::Locked);
						MainHud::isPaused = false;
					}
				}

				DebugStats::playerPos = transform.position;
				DebugStats::playerOrientation = transform.orientation;

				MainHud::update(dt, inventory);
			}
		}

		static void updateSurvival(float dt, Transform& transform, CharacterController& controller, Rigidbody& rb, Inventory& inventory)
		{
			blockPlaceDebounce -= dt;

			//Renderer::draw3DModel(transform.position + (glm::vec3(0.0f, 0.0f, 1.0f) * -1.0f * 2.7f), glm::vec3(1.0f), 0.0f, stick.vertices, stick.verticesLength);
			if (!MainHud::viewingCraftScreen && !CommandLine::isActive && !MainHud::isPaused)
			{
				RaycastStaticResult res = Physics::raycastStatic(transform.position + controller.cameraOffset, transform.forward, 5.0f);
				if (res.hit)
				{
					glm::vec3 blockLookingAtPos = res.point - (res.hitNormal * 0.1f);
					DebugStats::blockLookingAt = ChunkManager::getBlock(blockLookingAtPos);
					DebugStats::airBlockLookingAt = ChunkManager::getBlock(res.point + (res.hitNormal * 0.1f));

					// TODO: Clean this garbage up
					Renderer::drawBox(res.blockCenter, res.blockSize + glm::vec3(0.005f, 0.005f, 0.005f), blockHighlight);
					//Renderer::drawBox(res.point, glm::vec3(0.1f, 0.1f, 0.1f), Styles::defaultStyle);
					static float rotation = 0.0f;
					static glm::vec3 verticalOffset = glm::vec3(0.0f);
					static float speedDir = 0.05f;
					static int changeDirTick = 0;
					verticalOffset.y += speedDir * dt;
					//Renderer::drawTexturedCube(res.point + (res.hitNormal * 0.1f) + verticalOffset, glm::vec3(0.2f, 0.2f, 0.2f), *sideSprite, *topSprite, *bottomSprite, rotation);
					rotation = rotation + 30.0f * dt;
					changeDirTick++;
					if (changeDirTick > 30)
					{
						changeDirTick = 0;
						speedDir *= -1.0f;
					}
					if (rotation > 360.0f)
					{
						rotation = rotation / 360.0f;
					}

					if (Input::isMousePressed(GLFW_MOUSE_BUTTON_RIGHT) && blockPlaceDebounce <= 0)
					{
						static Block newBlock{
							0, 0, 0, 0
						};
						newBlock.id = inventory.hotbar[inventory.currentHotbarSlot].blockId;

						if (newBlock != BlockMap::NULL_BLOCK && newBlock != BlockMap::AIR_BLOCK && !newBlock.isItemOnly())
						{
							glm::vec3 worldPos = res.point + (res.hitNormal * 0.1f);
							ChunkManager::setBlock(worldPos, newBlock);
							blockPlaceDebounce = blockPlaceDebounceTime;
						}
					}
					else if (Input::isMousePressed(GLFW_MOUSE_BUTTON_LEFT) && blockPlaceDebounce <= 0)
					{
						glm::vec3 worldPos = res.point - (res.hitNormal * 0.1f);
						ChunkManager::removeBlock(worldPos);
						blockPlaceDebounce = blockPlaceDebounceTime;
					}
				}
				else
				{
					DebugStats::blockLookingAt = BlockMap::NULL_BLOCK;
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

				updateInventory(dt, inventory);
			}

			if (Input::keyBeginPress(GLFW_KEY_F4))
			{
				controller.controllerBaseSpeed = 4.4f;
				controller.controllerRunSpeed = 6.2f;
				gameMode = GameMode::Creative;
				MainHud::notify("Game Mode Creative");
			}

			if (!CommandLine::isActive && Input::keyBeginPress(GLFW_KEY_E))
			{
				MainHud::viewingCraftScreen = !MainHud::viewingCraftScreen;
				CursorMode mode = MainHud::viewingCraftScreen
					? CursorMode::Normal
					: CursorMode::Locked;
				Application::getWindow().setCursorMode(mode);
			}
		}

		static void updateCreative(float dt, Transform& transform, CharacterController& controller, Rigidbody& rb, Inventory& inventory)
		{
			static float doubleJumpDebounce = 0.0f;
			const float doubleJumpDebounceTime = 0.5f;
			blockPlaceDebounce -= dt;
			doubleJumpDebounce -= dt;

			if (!MainHud::viewingCraftScreen && !CommandLine::isActive && !MainHud::isPaused)
			{
				RaycastStaticResult res = Physics::raycastStatic(transform.position + controller.cameraOffset, transform.forward, 5.0f);
				if (res.hit)
				{
					glm::vec3 blockLookingAtPos = res.point - (res.hitNormal * 0.1f);
					DebugStats::blockLookingAt = ChunkManager::getBlock(blockLookingAtPos);
					DebugStats::airBlockLookingAt = ChunkManager::getBlock(res.point + (res.hitNormal * 0.1f));

					Renderer::drawBox(res.blockCenter, res.blockSize + glm::vec3(0.005f, 0.005f, 0.005f), blockHighlight);

					if (Input::isMousePressed(GLFW_MOUSE_BUTTON_RIGHT) && blockPlaceDebounce <= 0)
					{
						static Block newBlock{
							0, 0, 0, 0
						};
						newBlock.id = inventory.hotbar[inventory.currentHotbarSlot].blockId;

						if (newBlock != BlockMap::NULL_BLOCK && newBlock != BlockMap::AIR_BLOCK && !newBlock.isItemOnly())
						{
							glm::vec3 worldPos = res.point + (res.hitNormal * 0.1f);
							ChunkManager::setBlock(worldPos, newBlock);
							blockPlaceDebounce = blockPlaceDebounceTime;
						}
					}
					else if (Input::isMousePressed(GLFW_MOUSE_BUTTON_LEFT) && blockPlaceDebounce <= 0)
					{
						glm::vec3 worldPos = res.point - (res.hitNormal * 0.1f);
						ChunkManager::removeBlock(worldPos);
						blockPlaceDebounce = blockPlaceDebounceTime;
					}
				}
				else
				{
					DebugStats::blockLookingAt = BlockMap::NULL_BLOCK;
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
				if (!rb.useGravity)
				{
					controller.inMiddleOfJump = false;
					controller.movementAxis.y =
						Input::isKeyPressed(GLFW_KEY_SPACE)
						? 1.0f
						: Input::isKeyPressed(GLFW_KEY_LEFT_SHIFT)
						? -1.0f
						: 0.0f;
				}
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
						doubleJumpDebounce = doubleJumpDebounceTime;
					}
				}
				else if (!rb.onGround && doubleJumpDebounce < 0 && !rb.useGravity)
				{
					if (Input::keyBeginPress(GLFW_KEY_SPACE))
					{
						doubleJumpDebounce = doubleJumpDebounceTime;
					}
				}
				else if (doubleJumpDebounce >= 0)
				{
					// They just double tapped spacebar
					if (Input::keyBeginPress(GLFW_KEY_SPACE))
					{
						controller.applyJumpForce = false;
						controller.inMiddleOfJump = false;
						rb.useGravity = !rb.useGravity;
						if (!rb.useGravity)
						{
							rb.zeroForces();
							controller.controllerBaseSpeed *= 2.0f;
							controller.controllerRunSpeed *= 2.0f;
						}
						else
						{
							controller.controllerBaseSpeed /= 2.0f;
							controller.controllerRunSpeed /= 2.0f;
						}
						doubleJumpDebounce = -1.0f;
					}
				}

				updateInventory(dt, inventory);
			}

			if (Input::keyBeginPress(GLFW_KEY_F4))
			{
				if (rb.useGravity)
				{
					controller.controllerBaseSpeed *= 2.0f;
					controller.controllerRunSpeed *= 2.0f;
					rb.isSensor = true;
					rb.useGravity = false;
					rb.zeroForces();
					controller.inMiddleOfJump = false;
				}
				gameMode = GameMode::Spectator;
				MainHud::hotbarVisible = false;
				MainHud::notify("Game Mode Spectator");
			}

			if (!CommandLine::isActive && Input::keyBeginPress(GLFW_KEY_E))
			{
				MainHud::viewingCraftScreen = !MainHud::viewingCraftScreen;
				CursorMode mode = MainHud::viewingCraftScreen
					? CursorMode::Normal
					: CursorMode::Locked;
				Application::getWindow().setCursorMode(mode);
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
				controller.controllerBaseSpeed /= 2.0f;
				controller.controllerRunSpeed /= 2.0f;
				gameMode = GameMode::Survival;
				rb.isSensor = false;
				rb.useGravity = true;
				MainHud::hotbarVisible = true;
				MainHud::notify("Game Mode Survival");
			}
		}

		static void updateInventory(float dt, Inventory& inventory)
		{
			for (int i = 0; i < Player::numHotbarSlots; i++)
			{
				if (Input::keyBeginPress(GLFW_KEY_1 + i))
				{
					inventory.currentHotbarSlot = i;
				}
			}

			if (Input::mouseScrollY != 0)
			{
				inventory.currentHotbarSlot -= (int)Input::mouseScrollY;
				if (inventory.currentHotbarSlot < 0)
				{
					inventory.currentHotbarSlot = CMath::negativeMod(inventory.currentHotbarSlot, 0, Player::numHotbarSlots - 1);
				}
				else if (inventory.currentHotbarSlot >= Player::numHotbarSlots)
				{
					inventory.currentHotbarSlot = inventory.currentHotbarSlot % 9;
				}
			}
		}
	}
}