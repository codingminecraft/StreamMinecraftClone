#include "core.h"
#include "gui/MainHud.h"
#include "renderer/Sprites.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "renderer/Font.h"
#include "world/BlockMap.h"
#include "input/Input.h"
#include "gameplay/Inventory.h"

namespace Minecraft
{
	namespace MainHud
	{
		// External variables
		extern bool viewingCraftScreen = false;

		// Internal variables
		static const Sprite* blockCursorSprite = nullptr;
		static const Sprite* regularInventorySlot = nullptr;
		static const Sprite* selectedInventorySlot = nullptr;
		static const Sprite* inventoryHud = nullptr;
		static const Font* defaultFont = nullptr;

		static const glm::vec2 blockCursorSize = glm::vec2(0.1f, 0.1f);
		static const glm::vec2 inventorySlotSize = glm::vec2(0.2f, 0.2f);

		static const glm::vec2 craftingInventoryPixelSize = glm::vec2(216.0f, 194.0f);
		static const float craftingInventoryRatio = craftingInventoryPixelSize.y / craftingInventoryPixelSize.x;
		static const glm::vec2 craftingInventorySize = glm::vec2(2.4f, 2.4f * craftingInventoryRatio);
		static const glm::vec2 craftingInventoryPos = -(craftingInventorySize / 2.0f);
		// Inventory Slot Sizes are 24x24 pixels
		static const glm::vec2 craftingSlotSize = glm::vec2(21.0f, 21.0f) * (1.0f / craftingInventoryPixelSize) * craftingInventorySize;

		static std::array<glm::vec2, Player::numHotbarSlots + Player::numMainInventorySlots> slotPositions;

		static Style hoverStyle;

		// Internal functions
		static void initSlotPositions();
		static void updateCraftingScreen(Inventory& inventory);
		static void drawItemInSlot(InventorySlot block, const glm::vec2& slotPosition, const glm::vec2& slotSize, float itemSize, bool isMouseItem);
		static bool decrementMouseItem(InventorySlot& mouseItem, InventorySlot& inventorySlot);

		void init()
		{
			const robin_hood::unordered_map<std::string, Sprite>& menuSprites = Sprites::getSpritesheet("assets/images/hudSpritesheet.yaml");
			blockCursorSprite = &menuSprites.at(std::string("blockCursor"));
			regularInventorySlot = &menuSprites.at(std::string("regularInventorySlot"));
			selectedInventorySlot = &menuSprites.at(std::string("selectedInventorySlot"));
			inventoryHud = &menuSprites.at(std::string("inventoryHud"));
			defaultFont = Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);

			hoverStyle = Styles::defaultStyle;
			hoverStyle.color = "#00000044"_hex;

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
			if (viewingCraftScreen)
			{
				// TODO: I shouldn't have to do this, it's changing somehow because fonts probably
				hoverStyle.color = "#00000044"_hex;
				Renderer::drawFilledSquare2D(glm::vec2(-3.0f, -1.5f), glm::vec2(6.0f, 3.0f), hoverStyle, -5);
			}

			float startXPosition = -(Player::numHotbarSlots * inventorySlotSize.x) / 2.0f;
			glm::vec2 currentSlotPosition = glm::vec2(startXPosition, -1.48f);

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

			// Draw the crafting screen
			if (viewingCraftScreen)
			{
				updateCraftingScreen(inventory);
			}
		}

		void free()
		{
			blockCursorSprite = nullptr;
			regularInventorySlot = nullptr;
			selectedInventorySlot = nullptr;
		}

		static void updateCraftingScreen(Inventory& inventory)
		{
			static InventorySlot mouseItem = { BlockMap::NULL_BLOCK.id, 0 };
			static bool isHoldingItem = false;

			Renderer::drawTexture2D(*inventoryHud, craftingInventoryPos, craftingInventorySize, Styles::defaultStyle, -2);
			for (int i = 0; i < slotPositions.size(); i++)
			{
				bool leftClickedInventorySlot = false;
				bool rightClickedInventorySlot = false;
				glm::vec2 slotPosition = craftingInventoryPos + slotPositions[i];
				if (Input::mouseScreenX >= slotPosition.x && Input::mouseScreenX <= slotPosition.x + craftingSlotSize.x &&
					Input::mouseScreenY >= slotPosition.y && Input::mouseScreenY <= slotPosition.y + craftingSlotSize.y)
				{
					Renderer::drawFilledSquare2D(slotPosition, craftingSlotSize, hoverStyle, -1);

					leftClickedInventorySlot = Input::mouseBeginPress(GLFW_MOUSE_BUTTON_LEFT);
					rightClickedInventorySlot = Input::mouseBeginPress(GLFW_MOUSE_BUTTON_RIGHT);
				}

				InventorySlot inventoryBlock = inventory.hotbar[i];
				if (inventoryBlock.blockId != BlockMap::NULL_BLOCK.id && inventoryBlock.blockId != BlockMap::AIR_BLOCK.id)
				{
					drawItemInSlot(inventoryBlock, slotPosition, craftingSlotSize, 0.95f, false);
					if (leftClickedInventorySlot)
					{
						if (inventory.hotbar[i].blockId != mouseItem.blockId)
						{
							// Swap the block and mouse item if they are different
							inventory.hotbar[i] = mouseItem;
							mouseItem = inventoryBlock;
							isHoldingItem = true;
						}
						else
						{
							// Otherwise add the count to the mouse block up to 64
							inventory.hotbar[i].count += mouseItem.count;
							mouseItem.count = 0;
							if (inventory.hotbar[i].count > 64)
							{
								int leftover = inventory.hotbar[i].count - 64;
								inventory.hotbar[i].count = 64;
								mouseItem.count += leftover;
							}

							if (mouseItem.count == 0)
							{
								mouseItem.blockId = BlockMap::NULL_BLOCK.id;
								isHoldingItem = false;
							}
						}
					}
					else if (rightClickedInventorySlot)
					{
						if (mouseItem.blockId == BlockMap::NULL_BLOCK.id)
						{
							int halfCount = inventory.hotbar[i].count / 2;
							int mouseCount = inventory.hotbar[i].count - halfCount;
							int inventoryCount = inventory.hotbar[i].count - mouseCount;
							inventory.hotbar[i].count = inventoryCount;
							mouseItem = inventoryBlock;
							mouseItem.count = mouseCount;
							if (inventory.hotbar[i].count == 0)
							{
								inventory.hotbar[i].blockId = BlockMap::NULL_BLOCK.id;
							}
							isHoldingItem = true;
						}
						else
						{
							isHoldingItem = decrementMouseItem(mouseItem, inventory.hotbar[i]);
						}
					}
				}
				else if (leftClickedInventorySlot)
				{
					if (isHoldingItem)
					{
						InventorySlot tmp = mouseItem;
						mouseItem = inventory.slots[i];
						inventory.slots[i] = tmp;
						isHoldingItem = false;
					}
				}
				else if (rightClickedInventorySlot)
				{
					isHoldingItem = decrementMouseItem(mouseItem, inventory.hotbar[i]);
				}
			}

			if (isHoldingItem)
			{
				glm::vec2 mousePos = glm::vec2(Input::mouseScreenX, Input::mouseScreenY);
				drawItemInSlot(mouseItem, mousePos, craftingSlotSize, 1.0f, true);
			}
		}

		static void initSlotPositions()
		{
			// Pixel coords are 10x10 for first inventory slot
			glm::vec2 startPos = glm::vec2(10.0f, 10.0f) * (1.0f / craftingInventoryPixelSize) * craftingInventorySize;
			glm::vec2 currentSlotPosition = startPos;

			// The + 1 is because we have inventory slots and one extra row for the hotbar slots
			for (int row = 0; row < Player::numMainInventoryColumns + 1; row++)
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

				if (row == 0)
				{
					// The hotbar slots are 9 pixels below the inventory slots
					currentSlotPosition.y += (9.0f) * (1.0f / craftingInventoryPixelSize.y) * craftingInventorySize.y;
				}
			}
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
			if (mouseItem.blockId == inventorySlot.blockId || 
				(inventorySlot.blockId == BlockMap::NULL_BLOCK.id && mouseItem.blockId != BlockMap::NULL_BLOCK.id))
			{
				inventorySlot.count++;
				mouseItem.count--;
				if (mouseItem.count == 0)
				{
					mouseItem.blockId = BlockMap::NULL_BLOCK.id;
				}

				if (inventorySlot.blockId == BlockMap::NULL_BLOCK.id && inventorySlot.count > 0)
				{
					inventorySlot.blockId = mouseItem.blockId;
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