#include "core.h"
#include "core/Window.h"
#include "core/Input.h"
#include "core/Ecs.h"
#include "world/World.h"
#include "renderer/Shader.h"
#include "renderer/Renderer.h"
#include "renderer/Font.h"

namespace Minecraft
{
	namespace Application
	{
		// Settings
		static const int windowWidth = 1920;
		static const int windowHeight = 1080;
		static const glm::vec4 clearColor = glm::vec4(153.0f / 255.0f, 204.0f / 255.0f, 1.0f, 1.0f);
		static const char* windowTitle = "Minecraft Clone";
		static Window* window = nullptr;

		void run()
		{
			// Initiaize GLFW/Glad
			Ecs::Registry registry = Ecs::Registry();
			Window::init();
			window = Window::create(windowWidth, windowHeight, windowTitle);
			Renderer::init(registry);
			Fonts::init();

			if (!window)
			{
				return;
			}

			window->setVSync(true);
			World::init(registry);

			// Run game loop
			// Start with a 60 fps frame rate
			float previousTime = glfwGetTime() - 0.16f;
			bool isRunning = true;
			while (isRunning && !window->shouldClose())
			{
				float deltaTime = glfwGetTime() - previousTime;
				previousTime = glfwGetTime();

				Renderer::clearColor(clearColor);

				World::update(deltaTime);

				// Render
				window->swapBuffers();

				if (Input::isKeyPressed(GLFW_KEY_ESCAPE))
				{
					isRunning = false;
				}

				window->pollInput();
			}

			World::cleanup();
			Renderer::free();
			Window::cleanup();
		}

		void lockCursor(bool lock)
		{
			CursorMode mode = lock ?
				CursorMode::Locked :
				CursorMode::Normal;
			window->setCursorMode(mode);
		}

		float getAspectRatio()
		{
			return (float)window->width / (float)window->height;
		}
	}
}