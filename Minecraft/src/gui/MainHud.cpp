#include "core.h"
#include "core/Scene.h"
#include "gui/MainHud.h"
#include "gui/Gui.h"
#include "gui/GuiElements.h"
#include "renderer/Sprites.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "renderer/Font.h"
#include "world/BlockMap.h"
#include "input/Input.h"
#include "gameplay/Inventory.h"
#include "utils/CMath.h"
#include "network/Network.h"

namespace Minecraft
{
	namespace MainHud
	{
		// External variables
		bool viewingCraftScreen = false;
		bool isPaused = false;
		bool hotbarVisible = true;

		// Internal variables
		static const Sprite* blockCursorSprite = nullptr;
		static const Sprite* regularInventorySlot = nullptr;
		static const Sprite* selectedInventorySlot = nullptr;
		static const Sprite* inventoryHud = nullptr;
		static const Font* defaultFont = nullptr;
		static TexturedButton defaultButton;

		static const glm::vec2 blockCursorSize = glm::vec2(0.1f, 0.1f);
		static const glm::vec2 inventorySlotSize = glm::vec2(0.2f, 0.2f);

		static const glm::vec2 craftingInventoryPixelSize = glm::vec2(216.0f, 194.0f);
		static const float craftingInventoryRatio = craftingInventoryPixelSize.y / craftingInventoryPixelSize.x;
		static const glm::vec2 craftingInventorySize = glm::vec2(2.4f, 2.4f * craftingInventoryRatio);
		static const glm::vec2 craftingInventoryPos = -(craftingInventorySize / 2.0f);
		// Inventory Slot Sizes are 24x24 pixels
		static const glm::vec2 craftingSlotSize = glm::vec2(21.0f, 21.0f) * (1.0f / craftingInventoryPixelSize) * craftingInventorySize;

		static const float hotbarYPos = -1.48f;

		static std::array<glm::vec2, Player::numTotalSlots> slotPositions;
		static std::array<glm::vec2, 10> craftingSlotPositions;
		// We do +9 for the crafting slots
		static InventorySlot draggedSlots[Player::numTotalSlots + 9];
		static InventorySlot craftingSlots[10];

		static Style hoverStyle;

		static char* notificationMessage;
		static uint32 notificationMessageLength;
		static float timeToNotificationFadeout;
		static const float notificationDisplayTime = 1.0f;

		// Internal functions
		static void initSlotPositions();
		static void updatePauseScreen();
		static void updateNotificationMessage(float dt);
		static void updateCraftingScreen(Inventory& inventory);
		static void drawHotbar(const Inventory& inventory);
		static void drawItemInSlot(InventorySlot block, const glm::vec2& slotPosition, const glm::vec2& slotSize, float itemSize, bool isMouseItem);
		static bool decrementMouseItem(InventorySlot& mouseItem, InventorySlot& inventorySlot);
		static bool updateSlot(InventorySlot& inventorySlot, bool isDraggingRightClick, bool& isHoldingItem, InventorySlot& mouseItem, const glm::vec2& slotPosition, InventorySlot& draggedSlot, bool isCraftingOutput = false);
		static void checkIfItemIsCrafted();

		void init()
		{
			notificationMessage = nullptr;
			notificationMessageLength = 0;
			timeToNotificationFadeout = 0;

			viewingCraftScreen = false;
			isPaused = false;

			const robin_hood::unordered_map<std::string, Sprite>& menuSprites = Sprites::getSpritesheet("assets/images/hudSpritesheet.yaml");
			blockCursorSprite = &menuSprites.at(std::string("blockCursor"));
			regularInventorySlot = &menuSprites.at(std::string("regularInventorySlot"));
			selectedInventorySlot = &menuSprites.at(std::string("selectedInventorySlot"));
			inventoryHud = &menuSprites.at(std::string("inventoryHud"));
			defaultFont = Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);

			hoverStyle = Styles::defaultStyle;
			hoverStyle.color = "#00000044"_hex;
			defaultButton = *GuiElements::defaultButton;

			initSlotPositions();
		}

		void update(float dt, Inventory& inventory)
		{
			if (blockCursorSprite && !viewingCraftScreen)
			{
				Renderer::drawTexture2D(*blockCursorSprite, -blockCursorSize * 0.5f, blockCursorSize, Styles::defaultStyle);
			}

			// Check if sprites loaded properly
			if (!regularInventorySlot || !selectedInventorySlot || !inventoryHud)
			{
				g_logger_warning("Inventory slot textures not loaded properly.");
				return;
			}

			// Draw transparent overlay if needed
			if (viewingCraftScreen || isPaused)
			{
				// TODO: I shouldn't have to do this, it's changing somehow because fonts probably
				hoverStyle.color = "#00000044"_hex;
				Renderer::drawFilledSquare2D(glm::vec2(-3.0f, -1.5f), glm::vec2(6.0f, 3.0f), hoverStyle, -5);
			}

			if (hotbarVisible)
			{
				drawHotbar(inventory);
			}

			// Draw the crafting screen
			if (viewingCraftScreen)
			{
				updateCraftingScreen(inventory);
			}

			if (isPaused)
			{
				updatePauseScreen();
			}

			if (notificationMessage)
			{
				updateNotificationMessage(dt);
			}
		}

		void notify(const std::string& message)
		{
			if (notificationMessage != nullptr)
			{
				g_memory_free(notificationMessage);
			}

			notificationMessageLength = (uint32)message.length();
			notificationMessage = (char*)g_memory_allocate(sizeof(char) * (notificationMessageLength + 1));
			std::strcpy(notificationMessage, message.c_str());
			notificationMessage[notificationMessageLength] = '\0';
			timeToNotificationFadeout = notificationDisplayTime;
		}

		void free()
		{
			blockCursorSprite = nullptr;
			regularInventorySlot = nullptr;
			selectedInventorySlot = nullptr;

			if (notificationMessage)
			{
				g_memory_free(notificationMessage);
			}
		}

		static void updatePauseScreen()
		{
			Gui::beginWindow(glm::vec2(-1.5f, 1.5f), glm::vec2(3.0f, 3.0f));

			Gui::advanceCursor(glm::vec2(0.0f, 1.0f));
			Gui::centerNextElement();
			defaultButton.text = "Save and Exit";
			if (Gui::textureButton(defaultButton))
			{
				isPaused = false;
				Scene::changeScene(SceneType::MainMenu);
			}

			Gui::advanceCursor(glm::vec2(0.0f, 0.1f));
			Gui::centerNextElement();
			defaultButton.text = "Start LAN Server";
			// TODO: Re-enable me once multiplayer is working good
			//if (Gui::textureButton(defaultButton, Network::isLanServer()))
			if (Gui::textureButton(defaultButton, true))
			{
				Network::free();
				Network::init(true, "127.0.0.1", 8080);
			}

			Gui::advanceCursor(glm::vec2(0.0f, 0.1f));
			Gui::centerNextElement();
			defaultButton.text = "Settings";
			if (Gui::textureButton(defaultButton))
			{
				g_logger_info("OPENING SETTINGS");
			}

			Gui::endWindow();
		}

		static void updateNotificationMessage(float dt)
		{
			if (timeToNotificationFadeout >= 0)
			{
				float fadeOutStart = notificationDisplayTime * 0.5f;
				float alphaLevel = timeToNotificationFadeout >= (notificationDisplayTime - fadeOutStart)
					? 1.0f
					: timeToNotificationFadeout / (notificationDisplayTime - fadeOutStart);
				float yPos = hotbarYPos + inventorySlotSize.y + 0.1f;
				float fontScale = 0.0015f;
				const std::string messageStr = std::string(notificationMessage);
				glm::vec2 messageStrSize = defaultFont->getSize(messageStr, fontScale);
				float xPos = -messageStrSize.x / 2.0f;
				static Style notificationStyle = Styles::defaultStyle;
				notificationStyle.color.a = alphaLevel;
				Renderer::drawString(messageStr, *defaultFont, glm::vec2(xPos, yPos), fontScale, notificationStyle, 1);
			}

			timeToNotificationFadeout -= dt;
		}

		static void drawHotbar(const Inventory& inventory)
		{
			float startXPosition = -(Player::numHotbarSlots * inventorySlotSize.x) / 2.0f;
			glm::vec2 currentSlotPosition = glm::vec2(startXPosition, hotbarYPos);

			// Draw the hotbar
			for (int i = 0; i < Player::numHotbarSlots; i++)
			{
				if (i != inventory.currentHotbarSlot)
				{
					Renderer::drawTexture2D(*regularInventorySlot, currentSlotPosition, inventorySlotSize, Styles::defaultStyle);
				}
				else
				{
					const glm::vec2 bigSize = inventorySlotSize * 1.1f;
					glm::vec2 adjustedPosition = currentSlotPosition - glm::vec2(((bigSize.x - inventorySlotSize.x) / 2.0f), 0);
					Renderer::drawTexture2D(*selectedInventorySlot, adjustedPosition, bigSize, Styles::defaultStyle);
				}

				InventorySlot inventoryBlock = inventory.hotbar[i];
				if (inventoryBlock.blockId != BlockMap::NULL_BLOCK.id && inventoryBlock.blockId != BlockMap::AIR_BLOCK.id)
				{
					drawItemInSlot(inventoryBlock, currentSlotPosition, inventorySlotSize, 0.8f, false);
				}
				currentSlotPosition.x += inventorySlotSize.x;
			}
		}

		static void updateCraftingScreen(Inventory& inventory)
		{
			static InventorySlot mouseItem = { BlockMap::NULL_BLOCK.id, 0 };
			static bool isHoldingItem = false;
			static bool isDraggingRightClick = false;

			if (isDraggingRightClick)
			{
				isDraggingRightClick = Input::isMousePressed(GLFW_MOUSE_BUTTON_RIGHT);

				if (!isDraggingRightClick)
				{
					// If they let go of the mouse button, clear the dragged slots
					g_memory_zeroMem(draggedSlots, sizeof(draggedSlots));
				}
			}

			Renderer::drawTexture2D(*inventoryHud, craftingInventoryPos, craftingInventorySize, Styles::defaultStyle, -2);
			for (int i = 0; i < slotPositions.size(); i++)
			{
				glm::vec2 slotPosition = craftingInventoryPos + slotPositions[i];
				updateSlot(inventory.slots[i], isDraggingRightClick, isHoldingItem, mouseItem, slotPosition, draggedSlots[i]);
			}
			bool craftingSlotWasChanged = false;
			for (int i = 0; i < craftingSlotPositions.size() - 1; i++)
			{
				glm::vec2 slotPosition = craftingInventoryPos + craftingSlotPositions[i];
				craftingSlotWasChanged |= updateSlot(craftingSlots[i], isDraggingRightClick, isHoldingItem, mouseItem, slotPosition, draggedSlots[i + Player::numTotalSlots]);
			}
			isDraggingRightClick = Input::isMousePressed(GLFW_MOUSE_BUTTON_RIGHT);

			if (craftingSlotWasChanged)
			{
				checkIfItemIsCrafted();
			}

			if (!isHoldingItem && !isDraggingRightClick)
			{
				static InventorySlot fakeDraggedSlot = { 0, 0 };
				const glm::vec2 slotPosition = craftingInventoryPos + craftingSlotPositions[9];
				bool grabbedCraftingOutput = updateSlot(craftingSlots[9], false, isHoldingItem, mouseItem, slotPosition, fakeDraggedSlot, true);
				if (grabbedCraftingOutput)
				{
					for (int i = 0; i < 9; i++)
					{
						if (craftingSlots[i].blockId != BlockMap::NULL_BLOCK.id)
						{
							craftingSlots[i].count--;
							if (craftingSlots[i].count <= 0)
							{
								craftingSlots[i].count = 0;
								craftingSlots[i].blockId = BlockMap::NULL_BLOCK.id;
								draggedSlots[Player::numTotalSlots + i] = craftingSlots[i];
							}
						}
					}
					checkIfItemIsCrafted();
				}
			}
			else
			{
				static InventorySlot fakeDraggedSlot = { 0, 0 };
				const glm::vec2 slotPosition = craftingInventoryPos + craftingSlotPositions[9];
				updateSlot(craftingSlots[9], false, isHoldingItem, mouseItem, slotPosition, fakeDraggedSlot, true);
			}

			if (isHoldingItem)
			{
				glm::vec2 mousePos = glm::vec2(Input::mouseScreenX, Input::mouseScreenY);
				drawItemInSlot(mouseItem, mousePos, craftingSlotSize, 1.0f, true);
			}
		}

		static void checkIfItemIsCrafted()
		{
			const std::vector<CraftingRecipe>& allRecipes = BlockMap::getAllCraftingRecipes();

			for (auto& recipe : allRecipes)
			{
				int firstBlockIdInRecipe = recipe.blockIds[0];
				for (int row = 0; row < 3; row++)
				{
					for (int column = 0; column < 3; column++)
					{
						// The crafting slots are stored bottom -> up in the array
						// so flip the y axis here
						int blockId = craftingSlots[column + ((2 - row) * 3)].blockId;
						if (blockId == firstBlockIdInRecipe)
						{
							bool isMatch = true;
							for (int recipeRow = 0; recipeRow < 3 && isMatch; recipeRow++)
							{
								for (int recipeColumn = 0; recipeColumn < 3; recipeColumn++)
								{
									int recipeBlockId = recipeBlockId = recipe.blockIds[recipeColumn + (recipeRow * 3)];
									int currentBlockId = craftingSlots[((recipeColumn + column) % 3) + (CMath::negativeMod((2 - (recipeRow + row)), 0, 2) * 3)].blockId;
									if (recipeBlockId != currentBlockId)
									{
										isMatch = false;
										break;
									}
								}
							}

							if (isMatch)
							{
								craftingSlots[9].blockId = recipe.output;
								craftingSlots[9].count = recipe.outputCount;
								return;
							}
						}
					}
				}
			}

			// If we found no match, set the output to null
			craftingSlots[9].blockId = BlockMap::NULL_BLOCK.id;
			craftingSlots[9].count = 0;
		}

		static bool updateSlot(InventorySlot& inventorySlot, bool isDraggingRightClick, bool& isHoldingItem, InventorySlot& mouseItem, const glm::vec2& slotPosition, InventorySlot& draggedSlot, bool isCraftingOutput)
		{
			bool leftClickedInventorySlot = false;
			bool rightClickedInventorySlot = false;
			bool slotWasChanged = false;

			bool mouseOverSlot = Input::mouseScreenX >= slotPosition.x && Input::mouseScreenX <= slotPosition.x + craftingSlotSize.x &&
				Input::mouseScreenY >= slotPosition.y && Input::mouseScreenY <= slotPosition.y + craftingSlotSize.y;
			bool draggedOver = isDraggingRightClick && draggedSlot == inventorySlot &&
				draggedSlot.blockId != BlockMap::NULL_BLOCK.id;
			if (mouseOverSlot || draggedOver)
			{
				Renderer::drawFilledSquare2D(slotPosition, craftingSlotSize, hoverStyle, -1);

				if (mouseOverSlot && !draggedOver)
				{
					leftClickedInventorySlot = Input::mouseBeginPress(GLFW_MOUSE_BUTTON_LEFT);
					rightClickedInventorySlot = Input::mouseBeginPress(GLFW_MOUSE_BUTTON_RIGHT) || isDraggingRightClick;
				}
			}

			InventorySlot inventoryBlock = inventorySlot;
			if (inventoryBlock.blockId != BlockMap::NULL_BLOCK.id)
			{
				drawItemInSlot(inventoryBlock, slotPosition, craftingSlotSize, 0.95f, false);
				if (leftClickedInventorySlot)
				{
					slotWasChanged = true;
					if (inventorySlot.blockId != mouseItem.blockId && (!isCraftingOutput || mouseItem.blockId == BlockMap::NULL_BLOCK.id))
					{
						// Swap the block and mouse item if they are different
						inventorySlot = mouseItem;
						mouseItem = inventoryBlock;
						isHoldingItem = true;
					}
					else if (!isCraftingOutput)
					{
						// Otherwise add the count to the inventory block up to 64
						inventorySlot.count += mouseItem.count;
						mouseItem.count = 0;
						if (inventorySlot.count > 64)
						{
							int leftover = inventorySlot.count - 64;
							inventorySlot.count = 64;
							mouseItem.count += leftover;
						}

						if (mouseItem.count == 0)
						{
							mouseItem.blockId = BlockMap::NULL_BLOCK.id;
							isHoldingItem = false;
						}
					}
				}
				else if (rightClickedInventorySlot && !isCraftingOutput)
				{
					slotWasChanged = true;
					if (mouseItem.blockId == BlockMap::NULL_BLOCK.id && !isDraggingRightClick)
					{
						int halfCount = inventorySlot.count / 2;
						int mouseCount = inventorySlot.count - halfCount;
						int inventoryCount = inventorySlot.count - mouseCount;
						inventorySlot.count = inventoryCount;
						mouseItem = inventoryBlock;
						mouseItem.count = mouseCount;
						if (inventorySlot.count == 0)
						{
							inventorySlot.blockId = BlockMap::NULL_BLOCK.id;
						}
						isHoldingItem = true;
						draggedSlot = inventorySlot;
					}
					else
					{
						isHoldingItem = decrementMouseItem(mouseItem, inventorySlot);
						draggedSlot = inventorySlot;
					}
				}
			}
			else if (leftClickedInventorySlot && !isCraftingOutput)
			{
				slotWasChanged = true;
				if (isHoldingItem)
				{
					InventorySlot tmp = mouseItem;
					mouseItem = inventorySlot;
					inventorySlot = tmp;
					isHoldingItem = false;
				}
			}
			else if (rightClickedInventorySlot && !isCraftingOutput)
			{
				slotWasChanged = true;
				isHoldingItem = decrementMouseItem(mouseItem, inventorySlot);
				draggedSlot = inventorySlot;
			}

			return slotWasChanged;
		}

		static void initSlotPositions()
		{
			g_memory_zeroMem(draggedSlots, sizeof(draggedSlots));
			g_memory_zeroMem(craftingSlots, sizeof(craftingSlots));

			// Pixel coords are 10x10 for first inventory slot
			glm::vec2 startPos = glm::vec2(10.0f, 10.0f) * (1.0f / craftingInventoryPixelSize) * craftingInventorySize;
			glm::vec2 currentSlotPosition = startPos;
			float highestRowTopY = FLT_MIN;

			// The + 1 is because we have inventory slots and one extra row for the hotbar slots
			for (int row = 0; row < Player::numMainInventoryRows + 1; row++)
			{
				for (int column = 0; column < Player::numMainInventoryColumns; column++)
				{
					slotPositions[column + (row * Player::numMainInventoryColumns)] = currentSlotPosition;
					currentSlotPosition.x += craftingSlotSize.x;
					// Add one pixel padding
					currentSlotPosition.x += (1.0f) * (1.0f / craftingInventoryPixelSize.x) * craftingInventorySize.x;
				}
				currentSlotPosition.x = startPos.x;
				currentSlotPosition.y += craftingSlotSize.y;
				currentSlotPosition.y += (1.0f) * (1.0f / craftingInventoryPixelSize.y) * craftingInventorySize.y;
				highestRowTopY = glm::max(highestRowTopY, currentSlotPosition.y + (1.0f) * (1.0f / craftingInventoryPixelSize.y) * craftingInventorySize.y);

				if (row == 0)
				{
					// The hotbar slots are 9 pixels below the inventory slots
					currentSlotPosition.y += (9.0f) * (1.0f / craftingInventoryPixelSize.y) * craftingInventorySize.y;
				}
			}

			startPos.y = highestRowTopY + (12.0f) * (1.0f / craftingInventoryPixelSize.y) * craftingInventorySize.y;
			currentSlotPosition = startPos;
			g_logger_assert(highestRowTopY != FLT_MIN, "Did not find any rows for some reason...");
			float middleRightY = FLT_MIN;
			float middleRightX = FLT_MIN;
			// The + 1 is because we have inventory slots and one extra row for the hotbar slots
			for (int row = 0; row < 3; row++)
			{
				for (int column = 0; column < 3; column++)
				{
					craftingSlotPositions[column + (row * 3)] = currentSlotPosition;
					currentSlotPosition.x += craftingSlotSize.x;
					// Add one pixel padding
					currentSlotPosition.x += (1.0f) * (1.0f / craftingInventoryPixelSize.x) * craftingInventorySize.x;

					if (row == 1 && column == 2)
					{
						middleRightX = currentSlotPosition.x;
						middleRightY = currentSlotPosition.y;
					}
				}
				currentSlotPosition.x = startPos.x;
				currentSlotPosition.y += craftingSlotSize.y;
				currentSlotPosition.y += (1.0f) * (1.0f / craftingInventoryPixelSize.y) * craftingInventorySize.y;
			}

			g_logger_assert(middleRightX != FLT_MIN, "Failed to find the middle right x.");
			g_logger_assert(middleRightY != FLT_MIN, "Failed to find the middle right y.");
			glm::vec2 craftingOutputSlotPosition = glm::vec2(middleRightX, middleRightY);
			craftingOutputSlotPosition.x += (88.0f) * (1.0f / craftingInventoryPixelSize.x) * craftingInventorySize.x;
			craftingSlotPositions[9] = craftingOutputSlotPosition;
		}

		/// <summary>
		/// Decrements the mouse slot by one and increments the current slot position by one if the ids are the same
		/// or the inventory slot is empty
		/// </summary>
		/// <param name="mouseItem"></param>
		/// <param name="inventorySlot"></param>
		/// <returns>True if the mouse slot still contains items </returns>
		static bool decrementMouseItem(InventorySlot& mouseItem, InventorySlot& inventorySlot)
		{
			if ((mouseItem.blockId == inventorySlot.blockId && mouseItem.blockId != BlockMap::NULL_BLOCK.id) ||
				(inventorySlot.blockId == BlockMap::NULL_BLOCK.id && mouseItem.blockId != BlockMap::NULL_BLOCK.id))
			{
				inventorySlot.count++;
				mouseItem.count--;

				// If we placed a block in an empty slot, set the slot to the mouse's block id
				if (inventorySlot.blockId == BlockMap::NULL_BLOCK.id && inventorySlot.count > 0)
				{
					inventorySlot.blockId = mouseItem.blockId;
				}

				// If we placed our last item, set the mouse's block id to null
				if (mouseItem.count == 0)
				{
					mouseItem.blockId = BlockMap::NULL_BLOCK.id;
				}
			}

			return mouseItem.count != 0;
		}

		static void drawItemInSlot(InventorySlot block, const glm::vec2& slotPosition, const glm::vec2& slotSize, float itemSize, bool isMouseItem)
		{
			g_logger_assert(block.blockId != BlockMap::NULL_BLOCK.id && block.blockId != BlockMap::AIR_BLOCK.id, "Invalid block id. Cannot draw null or air block.");
			const BlockFormat& blockFormat = BlockMap::getBlock(block.blockId);
			const TextureFormat& format = BlockMap::getTextureFormat(blockFormat.itemPictureName);
			static Sprite sprite;
			sprite.texture = *format.texture;
			sprite.uvStart = format.uvs[3];
			sprite.uvSize = format.uvs[1] - sprite.uvStart;

			const glm::vec2 smallSize = slotSize * itemSize;
			glm::vec2 adjustedPosition = slotPosition + ((slotSize - smallSize) * 0.5f);
			if (isMouseItem)
			{
				adjustedPosition = slotPosition - (slotSize * 0.5f);
			}
			int zIndex = isMouseItem ? 3 : 1;
			Renderer::drawTexture2D(sprite, adjustedPosition, smallSize, Styles::defaultStyle, zIndex);

			if (block.count > 1)
			{
				// Draw the count
				const float fontScale = 0.0012f;
				const float padding = 0.001f;
				std::string blockCount = std::to_string(block.count);
				glm::vec2 countSize = defaultFont->getSize(blockCount, fontScale);
				glm::vec2 countPosition = (adjustedPosition + glm::vec2(smallSize.x, 0)) - glm::vec2((countSize).x + padding, 0);
				Renderer::drawString(blockCount, *defaultFont, countPosition, fontScale, Styles::defaultStyle, zIndex + 1);
			}
		}
	}
}