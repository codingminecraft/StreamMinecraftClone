#ifndef MINECRAFT_APPLICATION_H
#define MINECRAFT_APPLICATION_H
#include "core.h"

namespace Minecraft
{
	struct Window;

	namespace Application
	{
		void init();
		void run();
		void free();

		void takeScreenshot();

		Window& getWindow();
	}
}

#endif