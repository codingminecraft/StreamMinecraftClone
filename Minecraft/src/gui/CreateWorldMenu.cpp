#include "gui/CreateWorldMenu.h"
#include "gui/Gui.h"
#include "gui/GuiElements.h"
#include "core/Scene.h"
#include "core/AppData.h"
#include "world/World.h"

namespace Minecraft
{
	namespace CreateWorldMenu
	{
		// Internal variables
		static bool isCreatingNewWorld;

		// Internal functions
		static void showSavedWorlds();
		static void showCreateNewWorldMenu();

		void init()
		{
			isCreatingNewWorld = false;

			g_logger_info("Initialized settings menu.");
		}

		void update(float dt)
		{
			if (isCreatingNewWorld)
			{
				showCreateNewWorldMenu();
			}
			else
			{
				showSavedWorlds();
			}
		}

		void free()
		{

		}

		static void showSavedWorlds()
		{
			// Window 1
			Gui::beginWindow(glm::vec2(-1.5f, 1.5f), glm::vec2(3.0f, 2.5f));
			Gui::advanceCursor(glm::vec2(0.0f, 0.1f));

			TexturedButton button = *GuiElements::defaultButton;

			std::filesystem::path filepath = AppData::worldsRootPath;
			for (auto& worldPath : std::filesystem::directory_iterator(filepath))
			{
				Gui::centerNextElement();
				std::filesystem::path stem = worldPath.path().stem();
				std::string worldDataPath = stem.string();
				button.text = worldDataPath.c_str();
				if (Gui::textureButton(button))
				{
					World::savePath = worldDataPath;
				}
				Gui::advanceCursor(glm::vec2(0.0f, 0.05f));
			}

			Gui::endWindow();

			// Window 2
			Gui::beginWindow(glm::vec2(-1.5f, -1.0f), glm::vec2(3.0f, 0.5f));
			button.text = "Load World";
			button.size.x = 1.45f;
			// TODO: Make it so I can place this after the element I just drew
			Gui::sameLine();
			if (Gui::textureButton(button))
			{
				Scene::changeScene(SceneType::Game);
			}

			Gui::advanceCursor(glm::vec2(0.05f, 0.0f));
			button.text = "New World";
			if (Gui::textureButton(button))
			{
				isCreatingNewWorld = true;
			}

			Gui::endWindow();
		}

		static void showCreateNewWorldMenu()
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
	}
}
