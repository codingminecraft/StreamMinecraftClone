#include "input/Input.h"
#include "core/Scene.h"

namespace Minecraft
{
	namespace Input
	{
		float mouseScreenX = 0;
		float mouseScreenY = 0;
		float mouseX = 0;
		float mouseY = 0;
		float lastMouseX = 0;
		float lastMouseY = 0;
		float deltaMouseX = 0;
		float deltaMouseY = 0;
		float mouseScrollX = 0;
		float mouseScrollY = 0;
		bool keyPressed[GLFW_KEY_LAST] = {};
		bool keyBeginPressData[GLFW_KEY_LAST] = {};
		bool mousePressed[GLFW_MOUSE_BUTTON_LAST] = {};
		bool mouseBeginPressData[GLFW_MOUSE_BUTTON_LAST] = {};

		uint32 lastCharPressedData = '\0';

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
			Scene::queueMainEventMouseButton(button, action);
		}

		void mouseCallback(GLFWwindow* window, double xpos, double ypos)
		{
			Scene::queueMainEventMouse((float)xpos, (float)ypos);
		}

		void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
		{
			Scene::queueMainEventKey(key, action);
		}

		void scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
		{
			Scene::queueMainEventMouseScroll((float)xoffset, (float)yoffset);
		}

		void charCallback(GLFWwindow* window, unsigned int codepoint)
		{
			Scene::queueMainEventChar(codepoint);
		}

		void endFrame()
		{
			deltaMouseX = 0;
			deltaMouseY = 0;
			lastCharPressedData = '\0';
			mouseScrollX = 0;
			mouseScrollY = 0;
			g_memory_zeroMem(keyBeginPressData, sizeof(keyBeginPressData));
			g_memory_zeroMem(mouseBeginPressData, sizeof(mouseBeginPressData));
		}

		void processKeyEvent(int key, int action)
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

		void processMouseEvent(float xpos, float ypos)
		{
			mouseX = xpos;
			mouseY = ypos;
			if (mFirstMouse)
			{
				lastMouseX = xpos;
				lastMouseY = ypos;
				mFirstMouse = false;
			}

			deltaMouseX = xpos - lastMouseX;
			deltaMouseY = lastMouseY - ypos;
			lastMouseX = xpos;
			lastMouseY = ypos;

			glm::vec4 tmp = glm::vec4((mouseX / windowSize.x) * 2.0f - 1.0f, -((mouseY / windowSize.y) * 2.0f - 1.0f), 0, 1.0f);
			glm::vec4 projectedScreen = inverseProjectionMatrix * tmp;
			mouseScreenX = projectedScreen.x;
			mouseScreenY = projectedScreen.y;
		}

		void processMouseButton(int button, int action)
		{
			g_logger_assert(button >= 0 && button < GLFW_MOUSE_BUTTON_LAST, "Invalid mouse button.");
			if (action == GLFW_PRESS)
			{
				mousePressed[button] = true;
				mouseBeginPressData[button] = true;
			}
			else if (action == GLFW_RELEASE)
			{
				mousePressed[button] = false;
				mouseBeginPressData[button] = false;
			}
		}

		void processMouseScroll(float xoffset, float yoffset)
		{
			mouseScrollX = xoffset;
			mouseScrollY = yoffset;
		}

		void processChar(unsigned int codepoint)
		{
			lastCharPressedData = codepoint;
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

		bool mouseBeginPress(int mouseButton)
		{
			g_logger_assert(mouseButton >= 0 && mouseButton < GLFW_MOUSE_BUTTON_LAST, "Invalid mouse button.");
			return mouseBeginPressData[mouseButton];
		}

		uint32 lastCharPressed()
		{
			return lastCharPressedData;
		}
	}
}