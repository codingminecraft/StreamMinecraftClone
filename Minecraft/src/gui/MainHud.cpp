#include "core.h"
#include "gui/MainHud.h"
#include "renderer/Sprites.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "world/BlockMap.h"

namespace Minecraft
{
	namespace MainHud
	{
		// External variables
		extern int currentInventorySlot = 0;
		extern int hotbarBlockIds[9] = { 2, 3, 4, 6, 8, 9, 10, 11, 2 };

		// Internal variables
		static const Sprite* blockCursorSprite = nullptr;
		static const Sprite* regularInventorySlot = nullptr;
		static const Sprite* selectedInventorySlot = nullptr;

		static const glm::vec2 blockCursorSize = glm::vec2(0.1f, 0.1f);
		static const glm::vec2 inventorySlotSize = glm::vec2(0.2f, 0.2f);
		static int numHotBarInventorySlots = 9;

		void init()
		{
			const std::unordered_map<std::string, Sprite>& menuSprites = Sprites::getSpritesheet("assets/images/hudSpritesheet.yaml");
			blockCursorSprite = &menuSprites.at(std::string("blockCursor"));
			regularInventorySlot = &menuSprites.at(std::string("regularInventorySlot"));
			selectedInventorySlot = &menuSprites.at(std::string("selectedInventorySlot"));
		}

		void update(float dt)
		{
			if (blockCursorSprite)
			{
				Renderer::drawTexture2D(*blockCursorSprite, -blockCursorSize * 0.5f, blockCursorSize, Styles::defaultStyle);
			}

			if (regularInventorySlot && selectedInventorySlot)
			{
				float startXPosition = -(numHotBarInventorySlots * inventorySlotSize.x) / 2.0f;
				glm::vec2 currentSlotPosition = glm::vec2(startXPosition, -1.48f);

				for (int i = 0; i < numHotBarInventorySlots; i++)
				{
					if (i != currentInventorySlot)
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
					inventoryBlock.id = hotbarBlockIds[i];

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
			}
			else
			{
				g_logger_warning("Inventory slot textures not loaded properly.");
			}
		}

		void free()
		{
			blockCursorSprite = nullptr;
			regularInventorySlot = nullptr;
			selectedInventorySlot = nullptr;
		}
	}
}