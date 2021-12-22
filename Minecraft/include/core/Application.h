#ifndef MINECRAFT_APPLICATION_H
#define MINECRAFT_APPLICATION_H
#include "core.h"

namespace Minecraft
{
	struct Window;
	struct Framebuffer;
	class GlobalThreadPool;

	namespace Application
	{
		void init();
		void run();
		void free();

		void takeScreenshot(const char* filename = "", bool mustBeSquare = false);

		Window& getWindow();
		Framebuffer& getMainFramebuffer();
		GlobalThreadPool& getGlobalThreadPool();

		extern float deltaTime;
	}
}

#endif