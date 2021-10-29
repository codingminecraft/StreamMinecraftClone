#include "input/Input.h"

namespace Minecraft
{
	namespace Input
	{
		extern float mouseScreenX = 0;
		extern float mouseScreenY = 0;
		extern float mouseX = 0;
		extern float mouseY = 0;
		extern float deltaMouseX = 0;
		extern float deltaMouseY = 0;
		extern float mouseScrollX = 0;
		extern float mouseScrollY = 0;
		extern bool keyPressed[GLFW_KEY_LAST] = {};
		extern bool keyBeginPressData[GLFW_KEY_LAST] = {};
		extern bool mousePressed[GLFW_MOUSE_BUTTON_LAST] = {};

		uint32 lastCharPressedData = '\0';

		static float mLastMouseX = 0;
		static float mLastMouseY = 0;
		static bool mFirstMouse = true;
		static glm::vec2 windowSize = glm::vec2();
		static glm::mat4 inverseProjectionMatrix = glm::mat4();

		void setProjectionMatrix(const glm::mat4& inProjectionMatrix)
		{
			inverseProjectionMatrix = glm::inverse(inProjectionMatrix);
		}

		void setWindowSize(const glm::vec2& inWindowSize)
		{
			windowSize = inWindowSize;
		}

		void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
		{
			g_logger_assert(button >= 0 && button < GLFW_MOUSE_BUTTON_LAST, "Invalid mouse button.");
			if (action == GLFW_PRESS)
			{
				mousePressed[button] = true;
			}
			else if (action == GLFW_RELEASE)
			{
				mousePressed[button] = false;
			}
		}

		void mouseCallback(GLFWwindow* window, double xpos, double ypos)
		{
			mouseX = (float)xpos;
			mouseY = (float)ypos;
			if (mFirstMouse)
			{
				mLastMouseX = (float)xpos;
				mLastMouseY = (float)ypos;
				mFirstMouse = false;
			}

			deltaMouseX = (float)xpos - mLastMouseX;
			deltaMouseY = mLastMouseY - (float)ypos;
			mLastMouseX = (float)xpos;
			mLastMouseY = (float)ypos;

			glm::vec4 tmp = glm::vec4((mouseX / windowSize.x) * 2.0f - 1.0f, -((mouseY / windowSize.y) * 2.0f - 1.0f), 0, 1.0f);
			glm::vec4 projectedScreen = inverseProjectionMatrix * tmp;
			mouseScreenX = projectedScreen.x;
			mouseScreenY = projectedScreen.y;
		}

		void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
		{
			if (key < 0 || key > GLFW_KEY_LAST)
			{
				return;
			}

			if (action == GLFW_PRESS)
			{
				keyPressed[key] = true;
				keyBeginPressData[key] = true;
			}
			else if (action == GLFW_RELEASE)
			{
				keyPressed[key] = false;
				keyBeginPressData[key] = false;
			}
		}

		void scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
		{
			mouseScrollX = (float)xoffset;
			mouseScrollY = (float)yoffset;
		}

		void charCallback(GLFWwindow* window, unsigned int codepoint)
		{
			lastCharPressedData = codepoint;
		}

		void endFrame()
		{
			deltaMouseX = 0;
			deltaMouseY = 0;
			lastCharPressedData = '\0';
			mouseScrollX = 0;
			mouseScrollY = 0;
			g_memory_zeroMem(keyBeginPressData, sizeof(keyBeginPressData));
		}

		bool isKeyPressed(int key)
		{
			g_logger_assert(key >= 0 && key < GLFW_KEY_LAST, "Invalid key.");
			return keyPressed[key];
		}

		bool keyBeginPress(int key)
		{
			g_logger_assert(key >= 0 && key < GLFW_KEY_LAST, "Invalid key.");
			return keyBeginPressData[key];
		}

		bool isMousePressed(int mouseButton)
		{
			g_logger_assert(mouseButton >= 0 && mouseButton < GLFW_MOUSE_BUTTON_LAST, "Invalid mouse button.");
			return mousePressed[mouseButton];
		}

		uint32 lastCharPressed()
		{
			return lastCharPressedData;
		}
	}
}