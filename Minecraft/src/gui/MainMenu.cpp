#include "gui/MainMenu.h"
#include "gui/Gui.h"
#include "gui/GuiElements.h"
#include "gui/CreateWorldMenu.h"
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
		static Sprite title;
		static glm::vec2 titleSize;
		static bool isCreatingWorld;

		void init()
		{
			isCreatingWorld = false;

			const std::unordered_map<std::string, Sprite>& menuSprites = Sprites::getSpritesheet("assets/images/menuSpritesheet.yaml");

			titleSize = glm::vec2(2.0f, 0.5f);
			title = menuSprites.at(std::string("title"));

			g_logger_info("Initialized main menu scene.");
		}

		void update(float dt)
		{
			if (!isCreatingWorld)
			{
				Gui::beginWindow(glm::vec2(-1.5f, 1.5f), glm::vec2(3.0f, 3.0f));
				Gui::advanceCursor(glm::vec2(0.0f, 0.5f));
				Gui::centerNextElement();
				Gui::image(title, titleSize);

				Gui::advanceCursor(glm::vec2(0.0f, 0.35f));
				Gui::centerNextElement();
				static TexturedButton button = *GuiElements::defaultButton;
				button.text = "Play Game";
				if (Gui::textureButton(button))
				{
					isCreatingWorld = true;
				}

				Gui::advanceCursor(glm::vec2(0.0f, 0.15f));
				Gui::centerNextElement();
				button.text = "Quit";
				if (Gui::textureButton(button))
				{
					Application::getWindow().close();
				}
				Gui::endWindow();
			}
			else
			{
				CreateWorldMenu::update(dt);
			}
		}

		void free()
		{

		}
	}
}