#include "core.h"
#include "gui/MainHud.h"
#include "renderer/Sprites.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"

namespace Minecraft
{
	namespace MainHud
	{
		// External variables
		extern int currentInventorySlot = 0;

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
				glm::vec2 currentSlotPosition = glm::vec2(startXPosition, -1.4f);

				for (int i = 0; i < numHotBarInventorySlots; i++)
				{
					if (i != currentInventorySlot)
					{
						Renderer::drawTexture2D(*regularInventorySlot, currentSlotPosition, inventorySlotSize, Styles::defaultStyle);
					}
					else
					{
						Renderer::drawTexture2D(*selectedInventorySlot, currentSlotPosition, inventorySlotSize, Styles::defaultStyle);
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