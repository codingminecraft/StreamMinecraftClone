#ifndef MINECRAFT_WINDOW_H
#define MINECRAFT_WINDOW_H
#include "core.h"

namespace Minecraft
{
	enum class CursorMode : uint8
	{
		Hidden,
		Locked,
		Normal
	};

	struct Window
	{
		int width;
		int height;
		const char* title;
		void* windowPtr;

		void makeContextCurrent();

		void pollInput();

		void swapBuffers();

		void update(float dt);

		void setCursorMode(CursorMode cursorMode);

		bool shouldClose();

		void setVSync(bool on);

		void setTitle(const char* title);

		static void cleanup();

		static Window* create(int width, int height, const char* title);
		static void free(Window* window);
		static void init();
	};
}

#endif
