#include "core/Application.h"
#include "core.h"
#include "core/Window.h"
#include "core/Ecs.h"
#include "core/Scene.h"
#include "core/AppData.h"
#include "core/GlobalThreadPool.h"
#include "renderer/Renderer.h"
#include "renderer/Font.h"
#include "renderer/Framebuffer.h"
#include "renderer/Shader.h"
#include "renderer/Sprites.h"
#include "physics/Physics.h"
#include "input/Input.h"
#include "input/KeyBindings.h"
#include "utils/Constants.h"
#include "utils/Settings.h"
#include "gui/Gui.h"
#include "gui/GuiElements.h"

namespace Minecraft
{
	namespace Application
	{
		float deltaTime = 0.016f;

		// Internal variables
		static Framebuffer mainFramebuffer;
		static bool dumpScreenshot = false;
		static bool screenshotMustBeSquare = false;
		static std::string screenshotName = "";

		static Shader screenShader;
		static GlobalThreadPool* globalThreadPool;

		// Internal functions
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
			globalThreadPool = new GlobalThreadPool(std::thread::hardware_concurrency());
			AppData::init();
			Ecs::Registry& registry = getRegistry();
			Renderer::init(registry);
			Fonts::init();
			Physics::init();
			Scene::init(SceneType::MainMenu, registry);
			KeyBindings::init();
			Gui::init();
			GuiElements::init();

			// Allocate some GPU memory for basic geometry VAOs
			Vertices::init();

			// Initialize the framebuffers
			Texture opaqueTextureSpec;
			opaqueTextureSpec.type = TextureType::_2D;
			opaqueTextureSpec.width = window.width;
			opaqueTextureSpec.height = window.height;
			opaqueTextureSpec.magFilter = FilterMode::Linear;
			opaqueTextureSpec.minFilter = FilterMode::Linear;
			opaqueTextureSpec.wrapS = WrapMode::None;
			opaqueTextureSpec.wrapT = WrapMode::None;
			opaqueTextureSpec.format = ByteFormat::RGBA_16F;
			opaqueTextureSpec.path = (char*)"";

			Texture accumulationTextureSpec;
			accumulationTextureSpec.type = TextureType::_2D;
			accumulationTextureSpec.width = window.width;
			accumulationTextureSpec.height = window.height;
			accumulationTextureSpec.magFilter = FilterMode::Linear;
			accumulationTextureSpec.minFilter = FilterMode::Linear;
			accumulationTextureSpec.wrapS = WrapMode::None;
			accumulationTextureSpec.wrapT = WrapMode::None;
			accumulationTextureSpec.format = ByteFormat::RGBA_16F;
			accumulationTextureSpec.path = (char*)"";

			Texture revealTextureSpec;
			revealTextureSpec.type = TextureType::_2D;
			revealTextureSpec.width = window.width;
			revealTextureSpec.height = window.height;
			revealTextureSpec.magFilter = FilterMode::Linear;
			revealTextureSpec.minFilter = FilterMode::Linear;
			revealTextureSpec.wrapS = WrapMode::None;
			revealTextureSpec.wrapT = WrapMode::None;
			revealTextureSpec.format = ByteFormat::R8_F;
			revealTextureSpec.path = (char*)"";

			mainFramebuffer = FramebufferBuilder(window.width, window.height)
				.addColorAttachment(opaqueTextureSpec)
				.addColorAttachment(accumulationTextureSpec)
				.addColorAttachment(revealTextureSpec)
				.includeDepthStencilBuffer()
				.generate();
			mainFramebuffer.bind();
			glViewport(0, 0, window.width, window.height);
			mainFramebuffer.unbind();

			// Initialize rendering state for blitting the main framebuffer to the screen
			screenShader.compile("assets/shaders/MainFramebuffer.glsl");
		}

		void run()
		{
			// Run game loop
			// Start with a 60 fps frame rate
			Window& window = getWindow();
			double previousTime = glfwGetTime();
			bool inMainMenu = true;
			const float targetFps = 0.016f;
			const float nextTarget = 0.032f;
			while (!window.shouldClose())
			{

				double currentTime = glfwGetTime();
				deltaTime = (float)(currentTime - previousTime);

#ifdef _USE_OPTICK
				OPTICK_FRAME("Main Thread");
				OPTICK_EVENT();
#endif

				if (mainFramebuffer.width != getWindow().width || mainFramebuffer.height != getWindow().height)
				{
					mainFramebuffer.width = getWindow().width;
					mainFramebuffer.height = getWindow().height;
					mainFramebuffer.regenerate();
					mainFramebuffer.bind();
					glViewport(0, 0, getWindow().width, getWindow().height);
				}

				// TODO: You're trying to debug the black screen because of the glClear framebuffer thing
				mainFramebuffer.bind();
				const GLenum mainDrawBuffer[3] = { GL_COLOR_ATTACHMENT0, GL_NONE, GL_NONE };
				glDrawBuffers(3, mainDrawBuffer);
				const float zeroFillerVec[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
				glClearBufferfv(GL_COLOR, 0, zeroFillerVec);
				float one = 1;
				glClearBufferfv(GL_DEPTH, 0, &one);
				Scene::update(deltaTime);

				// Unbind all framebuffers and render the composited image
				glDisable(GL_DEPTH_TEST);
				glDepthMask(GL_TRUE);
				glDisable(GL_BLEND);

				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

				screenShader.bind();
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, mainFramebuffer.getColorAttachment(0).graphicsId);
				screenShader.uploadInt("uMainTexture", 0);

				glBindVertexArray(Vertices::fullScreenSpaceRectangleVao);
				glDrawArrays(GL_TRIANGLES, 0, 6);

				glEnable(GL_DEPTH_TEST);

				window.swapBuffers();
				window.pollInput();

				if (dumpScreenshot)
				{
					std::string filepath = screenshotName;
					if (screenshotName == "")
					{
						time_t rawtime;
						struct tm* timeinfo;
						char buffer[80];

						time(&rawtime);
						timeinfo = localtime(&rawtime);

						strftime(buffer, sizeof(buffer), "%d-%m-%Y %H.%M.%S", timeinfo);
						screenshotName = std::string(buffer);
						filepath = AppData::screenshotsPath + "/" + screenshotName + ".png";
					}

					uint8* pixels = (uint8*)g_memory_allocate(sizeof(uint8) * mainFramebuffer.width * mainFramebuffer.height * 4);
					int outputWidth = mainFramebuffer.width;
					int outputHeight = mainFramebuffer.height;
					int startX = 0;
					int startY = 0;
					if (screenshotMustBeSquare)
					{
						if (outputWidth > outputHeight)
						{
							outputWidth = outputHeight;
							startX = (outputWidth - outputHeight) / 2;
						}
						else
						{
							outputHeight = outputWidth;
							startY = (outputHeight - outputWidth) / 2;
						}
					}

					glReadPixels(startX, startY, outputWidth, outputHeight, GL_RGBA, GL_UNSIGNED_BYTE, (void*)pixels);
					stbi_flip_vertically_on_write(true);
					stbi_write_png(filepath.c_str(), outputWidth, outputHeight, 4, (void*)pixels, sizeof(uint8) * outputWidth * 4);
					g_logger_info("Screenshot saved to: %s", filepath.c_str());
					g_memory_free(pixels);
					dumpScreenshot = false;
				}

				previousTime = currentTime;
			}
		}

		void free()
		{
			// Free our assets
			screenShader.destroy();
			Sprites::freeAllSpritesheets();
			Fonts::unloadAllFonts();
			mainFramebuffer.destroy();

			// Free all resources
			// Important: Scene gets freed first so that it queues all saving tasks to the global thread pool.
			// Then we can free the global thread pool which will finish those tasks
			Scene::free();
			globalThreadPool->free();
			delete globalThreadPool;

			Vertices::free();
			GuiElements::free();
			getRegistry().free();
			Window& window = getWindow();
			window.destroy();
			Renderer::free();
			Window::free();

			// Free the pointers now that everything should be cleaned up
			freeWindow();
			freeRegistry();
		}

		Window& getWindow()
		{
			static Window* window = Window::create(Settings::Window::title);
			return *window;
		}

		Framebuffer& getMainFramebuffer()
		{
			return mainFramebuffer;
		}

		GlobalThreadPool& getGlobalThreadPool()
		{
			return *globalThreadPool;
		}

		void takeScreenshot(const char* filename, bool mustBeSquare)
		{
			dumpScreenshot = true;
			screenshotName = filename;
			screenshotMustBeSquare = mustBeSquare;
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
			Ecs::Registry& registry = getRegistry();
			registry.free();
			delete &registry;
		}
	}
}