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
		bool shouldClose();

		void close();
		void destroy();
		void setCursorMode(CursorMode cursorMode);
		void setVSync(bool on);
		void setTitle(const char* title);
		void setSize(int width, int height);
		float getAspectRatio() const;

		static Window* create(const char* title);
		static void init();
		static void free();
	};
}

#endif
