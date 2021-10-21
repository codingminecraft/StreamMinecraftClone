#include "gui/CreateWorldMenu.h"
#include "gui/Gui.h"
#include "gui/GuiElements.h"
#include "core/Scene.h"
#include "world/World.h"

namespace Minecraft
{
	namespace CreateWorldMenu
	{
		void init()
		{
			const std::unordered_map<std::string, Sprite>& menuSprites = Sprites::getSpritesheet("assets/images/menuSpritesheet.yaml");

			//titleSize = glm::vec2(2.0f, 0.5f);
			//title = menuSprites.at(std::string("title"));

			g_logger_info("Initialized settings menu.");
		}

		void update(float dt)
		{
			Gui::beginWindow(glm::vec2(-1.5f, 1.5f), glm::vec2(3.0f, 3.0f));
			Gui::advanceCursor(glm::vec2(0.0f, 0.5f));
			//Gui::fillNextElement();
			static char worldSaveTitle[128];
			if (Gui::input("World Name: ", 0.0025f, worldSaveTitle, 128))
			{
				World::savePath = std::string(worldSaveTitle);
			}

			Gui::centerNextElement();
			Gui::advanceCursor(glm::vec2(0.0f, 0.05f));

			static TexturedButton button = *GuiElements::defaultButton;
			button.text = "Create World";
			if (Gui::textureButton(button))
			{
				Scene::changeScene(SceneType::Game);
			}

			Gui::endWindow();
		}

		void free()
		{

		}
	}
}
