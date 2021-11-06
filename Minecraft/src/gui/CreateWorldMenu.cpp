#include "gui/CreateWorldMenu.h"
#include "gui/Gui.h"
#include "gui/GuiElements.h"
#include "core/Scene.h"
#include "core/AppData.h"
#include "world/BlockMap.h"
#include "world/World.h"
#include "renderer/Styles.h"
#include "renderer/Renderer.h"

namespace Minecraft
{
	namespace CreateWorldMenu
	{
		// Internal variables
		static bool isCreatingNewWorld;
		static Sprite dirtTextureSprite;

		// Internal functions
		static void showSavedWorlds();
		static void showCreateNewWorldMenu();

		void init()
		{
			isCreatingNewWorld = false;

			g_logger_info("Initialized settings menu.");

			const TextureFormat& dirtTextureFormat = BlockMap::getTextureFormat("dirt");
			dirtTextureSprite.texture = *dirtTextureFormat.texture;
			dirtTextureSprite.uvSize = dirtTextureFormat.uvs[1] - dirtTextureFormat.uvs[3];
			dirtTextureSprite.uvStart = dirtTextureFormat.uvs[3];
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
			static Style dirtStyle = Styles::defaultStyle;
			dirtStyle.color = "#999999ff"_hex;
			Renderer::drawTexture2D(dirtTextureSprite, glm::vec2(-1.5f, -1.5f), glm::vec2(3.0f, 3.0f), dirtStyle, -3);
			Renderer::drawTexture2D(dirtTextureSprite, glm::vec2(-3.0f, -1.5f), glm::vec2(6.0f, 3.0f), Styles::defaultStyle, -4);

			// Window 1 holds all of the save files
			Gui::beginWindow(glm::vec2(-1.5f, 1.5f), glm::vec2(3.0f, 2.5f));
			Gui::advanceCursor(glm::vec2(0.0f, 0.1f));

			TexturedButton button = *GuiElements::defaultButton;

			static int selectedIndex = -1;

			std::filesystem::path filepath = AppData::worldsRootPath;
			int i = 0;
			for (auto& worldPath : std::filesystem::directory_iterator(filepath))
			{
				Gui::centerNextElement();
				std::filesystem::path stem = worldPath.path().stem();
				std::string worldDataPath = stem.string();
				button.text = worldDataPath.c_str();
				button.size.y = 0.3f;
				if (Gui::worldSaveItem(worldDataPath.c_str(), button.size, selectedIndex == i))
				{
					World::savePath = worldDataPath;
					selectedIndex = i;
				}
				Gui::advanceCursor(glm::vec2(0.0f, 0.05f));

				i++;
			}

			Gui::endWindow();

			// Window 2, this holds the load world and new world buttons
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
