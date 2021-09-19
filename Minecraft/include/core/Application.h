#ifndef MINECRAFT_APPLICATION_H
#define MINECRAFT_APPLICATION_H

namespace Minecraft
{
	struct Window;

	namespace Application
	{
		void init();
		void run();
		void free();

		Window& getWindow();
	}
}

#endif