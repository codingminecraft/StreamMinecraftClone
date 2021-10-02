#ifndef MINECRAFT_APPLICATION_H
#define MINECRAFT_APPLICATION_H
#include "core.h"

namespace Minecraft
{
	struct Window;

	enum class GameScene : uint8
	{
		MainMenu,
		Game
	};

	namespace Application
	{
		void init();
		void run();
		void free();
		void setScene(GameScene scene);

		Window& getWindow();
	}
}

#endif