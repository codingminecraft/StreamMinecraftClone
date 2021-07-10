#pragma once

#include "core.h"

namespace Minecraft
{
	namespace Input
	{
		extern int mouseX;
		extern int mouseY;
		extern int deltaMouseX;
		extern int deltaMouseY;
		extern bool keyPressed[GLFW_KEY_LAST];

		void mouseCallback(GLFWwindow* window, double xpos, double ypos);
		void endFrame();
		
		void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
		bool isKeyPressed(int key);
	}
}