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

		static Sprite title;
		static glm::vec2 titlePosition;
		static glm::vec2 titleSize;

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

			const std::unordered_map<std::string, Sprite>& menuSprites = Sprites::getSpritesheet("assets/images/menuSpritesheet.yaml");

			button.sprite = menuSprites.at(std::string("buttonRegular"));
			button.hoverSprite = menuSprites.at(std::string("buttonHover"));
			button.clickSprite = menuSprites.at(std::string("buttonClick"));
			button.font = Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);
			button.textScale = 0.5f;

			titleSize = glm::vec2(2.0f, 0.5f);
			titlePosition = glm::vec2(-1.0f, 0.6f);
			title = menuSprites.at(std::string("title"));

			g_logger_info("Initialized main menu scene.");
		}

		void update(float dt)
		{
			Renderer::drawTexture2D(title, titlePosition, titleSize, Styles::defaultStyle);

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