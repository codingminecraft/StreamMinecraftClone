#include "gui/MainMenu.h"
#include "gui/Gui.h"
#include "gui/GuiElements.h"
#include "gui/CreateWorldMenu.h"
#include "renderer/Texture.h"
#include "renderer/Font.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "renderer/Shader.h"
#include "renderer/Cubemap.h"
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

		static Cubemap skybox;
		static glm::mat4 projectionMatrix;
		static glm::mat4 viewMatrix;
		static glm::vec3 viewAxis;
		static float viewRotation;
		static Shader cubemapShader;

		void init()
		{
			isCreatingWorld = false;

			const robin_hood::unordered_map<std::string, Sprite>& menuSprites = Sprites::getSpritesheet("assets/images/hudSpritesheet.yaml");

			titleSize = glm::vec2(2.0f, 0.5f);
			title = menuSprites.at(std::string("title"));

			skybox = Cubemap::generateCubemap(
				"assets/images/menuSkybox/Top.png",
				"assets/images/menuSkybox/Bottom.png",
				"assets/images/menuSkybox/Left.png",
				"assets/images/menuSkybox/Right.png",
				"assets/images/menuSkybox/Front.png",
				"assets/images/menuSkybox/Back.png");
			cubemapShader.compile("assets/shaders/Cubemap.glsl");
			viewAxis = glm::normalize(glm::vec3(0.1f, 1.0f, -0.1f));
			viewRotation = 0.0f;

			g_logger_info("Initialized main menu scene.");
		}

		void update(float dt)
		{
			if (!isCreatingWorld)
			{
				projectionMatrix = glm::perspective(
					45.0f,
					(float)Application::getWindow().width / (float)Application::getWindow().height,
					0.1f,
					2000.0f
				);
				
				viewMatrix = glm::rotate(glm::radians(viewRotation), viewAxis);
				skybox.render(cubemapShader, projectionMatrix, viewMatrix);
				viewRotation = viewRotation - (3.0f * dt);
				if (viewRotation < 0)
				{
					viewRotation = 360.0f + viewRotation;
				}

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