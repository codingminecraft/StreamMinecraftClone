#ifndef MINECRAFT_INPUT_H
#define MINECRAFT_INPUT_H

#include "core.h"

namespace Minecraft
{
	namespace Input
	{
		extern float mouseX;
		extern float mouseY;
		extern float deltaMouseX;
		extern float deltaMouseY;
		extern bool keyPressed[GLFW_KEY_LAST];
		extern bool keyBeginPressData[GLFW_KEY_LAST];

		void mouseCallback(GLFWwindow* window, double xpos, double ypos);
		void endFrame();
		
		void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
		bool isKeyPressed(int key);
		bool keyBeginPress(int key);
	}
}

#endif