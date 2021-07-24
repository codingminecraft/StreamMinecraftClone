#ifndef MINECRAFT_WINDOW_H
#define MINECRAFT_WINDOW_H

namespace Minecraft
{
	enum CursorMode
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

		static void cleanup();

		static Window* create(int width, int height, const char* title);
		static void free(Window* window);
	};
}

#endif
