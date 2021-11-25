#include "gui/GuiElements.h"
#include "gui/Gui.h"
#include "renderer/Font.h"

namespace Minecraft
{
	namespace GuiElements
	{
		TexturedButton* defaultButton = nullptr;

		// Internal variables
		static Texture menuTextures;

		void init()
		{
			const robin_hood::unordered_map<std::string, Sprite>& menuSprites = Sprites::getSpritesheet("assets/images/hudSpritesheet.yaml");
			defaultButton = new TexturedButton();
			defaultButton->sprite = menuSprites.at(std::string("buttonRegular"));
			defaultButton->hoverSprite = menuSprites.at(std::string("buttonHover"));
			defaultButton->clickSprite = menuSprites.at(std::string("buttonClick"));
			defaultButton->font = Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);
			defaultButton->size = glm::vec2(2.5f, 0.2f);
			defaultButton->textScale = 0.0025f;

			g_logger_info("Initialized basic GUI elements.");
		}

		void free()
		{
			if (defaultButton)
			{
				delete defaultButton;
				defaultButton = nullptr;
			}
		}
	}
}