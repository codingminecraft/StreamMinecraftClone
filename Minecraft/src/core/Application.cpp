#include "core/Application.h"
#include "core.h"
#include "core/Window.h"
#include "core/Ecs.h"
#include "world/World.h"
#include "renderer/Shader.h"
#include "renderer/Renderer.h"
#include "renderer/Font.h"
#include "physics/Physics.h"
#include "input/Input.h"
#include "input/KeyBindings.h"
#include "utils/Settings.h"
#include "gameplay/PlayerController.h"

namespace Minecraft
{
	namespace Application
	{
		static Ecs::Registry& getRegistry();
		static void freeWindow();
		static void freeRegistry();

		void init()
		{
			// Initialize GLFW/Glad
			Window::init();
			Window& window = getWindow();
			if (!window.windowPtr)
			{
				g_logger_error("Error: Could not create window.");
				return;
			}

			// Initialize all other subsystems
			Ecs::Registry& registry = getRegistry();
			Renderer::init(registry);
			Fonts::init();
			Physics::init();
			World::init(registry);
			KeyBindings::init();

			// TODO: Should probably move this into World
			PlayerController::init();
		}

		void run()
		{
			// Run game loop
			// Start with a 60 fps frame rate
			Window& window = getWindow();
			float previousTime = (float)glfwGetTime() - 0.16f;
			while (!window.shouldClose())
			{
				float deltaTime = (float)glfwGetTime() - previousTime;
				previousTime = (float)glfwGetTime();

				Renderer::clearColor(Settings::Window::clearColor);
				World::update(deltaTime);

				window.swapBuffers();
				window.pollInput();
			}
		}

		void free()
		{
			// Free all resources
			Window& window = getWindow();
			window.destroy();
			World::free();
			Renderer::free();
			Window::free();

			// Free the pointers now that everything should be cleaned up
			freeWindow();
			freeRegistry();
		}

		Window& getWindow()
		{
			static Window* window = Window::create(Settings::Window::width, Settings::Window::height, Settings::Window::title);
			return *window;
		}

		static Ecs::Registry& getRegistry()
		{
			static Ecs::Registry* registry = new Ecs::Registry();
			return *registry;
		}

		static void freeWindow()
		{
			delete &getWindow();
		}

		static void freeRegistry()
		{
			delete &getRegistry();
		}
	}
}