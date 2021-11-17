#include "core.h"
#include "gui/MainHud.h"
#include "renderer/Sprites.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
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

		void init()
		{
			const robin_hood::unordered_map<std::string, Sprite>& menuSprites = Sprites::getSpritesheet("assets/images/hudSpritesheet.yaml");
			blockCursorSprite = &menuSprites.at(std::string("blockCursor"));
			regularInventorySlot = &menuSprites.at(std::string("regularInventorySlot"));
			selectedInventorySlot = &menuSprites.at(std::string("selectedInventorySlot"));
			inventoryHud = &menuSprites.at(std::string("inventoryHud"));

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

			if (!regularInventorySlot || !selectedInventorySlot || !inventoryHud)
			{
				g_logger_warning("Inventory slot textures not loaded properly.");
				return;
			}

			// Draw transparent overlay if needed
			if (viewingCraftScreen)
			{
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

				static Block inventoryBlock{
					0, 0, 0, 0
				};
				inventoryBlock.id = inventory.hotbar[i].blockId;

				if (inventoryBlock != BlockMap::NULL_BLOCK && inventoryBlock != BlockMap::AIR_BLOCK)
				{
					const BlockFormat& blockFormat = BlockMap::getBlock(inventoryBlock.id);
					const TextureFormat& format = BlockMap::getTextureFormat(blockFormat.itemPictureName);
					static Sprite sprite;
					sprite.texture = *format.texture;
					sprite.uvStart = format.uvs[3];
					sprite.uvSize = format.uvs[1] - sprite.uvStart;

					const glm::vec2 smallSize = inventorySlotSize * 0.8f;
					glm::vec2 adjustedPosition = currentSlotPosition + ((inventorySlotSize - smallSize) * 0.5f);
					Renderer::drawTexture2D(sprite, adjustedPosition, smallSize, Styles::defaultStyle, 1);
				}
				currentSlotPosition.x += inventorySlotSize.x;
			}

			// Draw the crafting screen
			if (viewingCraftScreen)
			{
				Renderer::drawTexture2D(*inventoryHud, craftingInventoryPos, craftingInventorySize, Styles::defaultStyle, -2);
				for (int i = 0; i < slotPositions.size(); i++)
				{
					glm::vec2 slotPosition = craftingInventoryPos + slotPositions[i];
					if (Input::mouseScreenX >= slotPosition.x && Input::mouseScreenX <= slotPosition.x + craftingSlotSize.x &&
						Input::mouseScreenY >= slotPosition.y && Input::mouseScreenY <= slotPosition.y + craftingSlotSize.y)
					{
						//Renderer::drawTexture2D(*selectedInventorySlot, currentSlotPosition, slotSize2, Styles::defaultStyle, -1);
						Renderer::drawFilledSquare2D(slotPosition, craftingSlotSize, hoverStyle, -1);
					}
					else
					{
						//Renderer::drawFilledSquare2D(slotPosition, craftingSlotSize, hoverStyle, -1);
					}
				}
			}
		}

		void free()
		{
			blockCursorSprite = nullptr;
			regularInventorySlot = nullptr;
			selectedInventorySlot = nullptr;
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
					currentSlotPosition.x += (1.0f)* (1.0f / craftingInventoryPixelSize.x)* craftingInventorySize.x;
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
	}
}