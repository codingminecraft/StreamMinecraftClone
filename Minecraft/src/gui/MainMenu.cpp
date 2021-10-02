#include "gui/MainMenu.h"
#include "gui/Gui.h"
#include "renderer/Texture.h"
#include "renderer/Font.h"
#include "renderer/Renderer.h"

namespace Minecraft
{
	namespace MainMenu
	{
		static Texture menuTextures;
		static TexturedButton button;

		void init()
		{
			menuTextures = TextureBuilder()
				.setFilepath("assets/images/menuSprites.png")
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setTextureType(TextureType::_2D)
				.setWrapS(WrapMode::None)
				.setWrapT(WrapMode::None)
				.generate(true);

			Sprite buttonSprite;
			buttonSprite.texture = &menuTextures;
			buttonSprite.uvSize = glm::vec2(272.0f / (float)menuTextures.width, 32.0f / (float)menuTextures.height);
			buttonSprite.uvStart = glm::vec2(0, 0);

			Sprite buttonHoverSprite;
			buttonHoverSprite.texture = &menuTextures;
			buttonHoverSprite.uvSize = buttonSprite.uvSize;
			buttonHoverSprite.uvStart = glm::vec2(0, 33.0f / (float)menuTextures.height);
			
			Sprite buttonClickSprite;
			buttonClickSprite.texture = &menuTextures;
			buttonClickSprite.uvSize = buttonSprite.uvSize;
			buttonClickSprite.uvStart = glm::vec2(0, 66.0f / (float)menuTextures.height);

			button.sprite = buttonSprite;
			button.hoverSprite = buttonHoverSprite;
			button.clickSprite = buttonClickSprite;
			button.font = Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);
			button.textScale = 0.3f;
		}

		void update(float dt)
		{
			button.text = "Play Game";
			button.position = glm::vec2(0, 0);
			button.size = glm::vec2(2.0f, 1.0f);
			if (Gui::textureButton(button))
			{
				g_logger_info("PLAY GAME!!");
			}

			// Do line rendering type stuff
			Renderer::render();
		}
	}
}