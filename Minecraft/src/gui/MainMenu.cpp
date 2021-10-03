#include "gui/MainMenu.h"
#include "gui/Gui.h"
#include "renderer/Texture.h"
#include "renderer/Font.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "core/Application.h"
#include "core/Window.h"
#include "core/Scene.h"

namespace Minecraft
{
	namespace MainMenu
	{
		static Texture menuTextures;
		static TexturedButton button;
		static RenderableTexture title;

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

			title.size = glm::vec2(2.0f, 0.5f);
			title.start = glm::vec2(-1.0f, 0.6f);
			title.texCoordSize = glm::vec2(88.0f / 512.0f, 19.0f / 512.0f);
			title.texCoordStart = glm::vec2(0, 100.0f / 512.0f);
			title.texture = &menuTextures;

			button.sprite = buttonSprite;
			button.hoverSprite = buttonHoverSprite;
			button.clickSprite = buttonClickSprite;
			button.font = Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);
			button.textScale = 0.5f;

			g_logger_info("Initialized main menu scene.");
		}

		void update(float dt)
		{
			Renderer::drawTexture2D(title, Styles::defaultStyle);

			button.text = "Play Game";
			button.position = glm::vec2(-1.25f, 0.2f);
			button.size = glm::vec2(2.5f, 0.2f);
			if (Gui::textureButton(button))
			{
				Scene::changeScene(SceneType::Game);
			}

			button.text = "Quit";
			button.position = glm::vec2(-1.25f, -0.1f);
			button.size = glm::vec2(2.5f, 0.2f);
			if (Gui::textureButton(button))
			{
				Application::getWindow().close();
			}
		}

		void free()
		{
			menuTextures.destroy();
		}
	}
}