#include "core.h"
#include "core/Window.h"
#include "core/Input.h"
#include "world/World.h"
#include "renderer/Shader.h"

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
			bool isRunning = true;

			window = Window::create(windowWidth, windowHeight, windowTitle);
			if (!window)
			{
				return;
			}

			window->setVSync(true);

			World::init();

			// Run game loop
			// Start with a 60 fps frame rate
			float previousTime = glfwGetTime() - 0.16f;
			while (isRunning && !window->shouldClose())
			{
				float deltaTime = glfwGetTime() - previousTime;
				previousTime = glfwGetTime();
				std::string title = "Minecraft -- dt " + std::to_string(deltaTime);
				window->setTitle(title.c_str());

				glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
			Window::cleanup();
		}

		void lockCursor(bool lock)
		{
			CursorMode mode = lock ?
				CursorMode::Locked :
				CursorMode::Normal;
			window->setCursorMode(mode);
		}
	}
}