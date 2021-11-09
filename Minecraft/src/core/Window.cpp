#include "core.h"
#include "core/Window.h"
#include "input/Input.h"

namespace Minecraft
{
	static void resizeCallback(GLFWwindow* windowPtr, int newWidth, int newHeight)
	{
		Window* userWindow = (Window*)glfwGetWindowUserPointer(windowPtr);
		userWindow->width = newWidth;
		userWindow->height = newHeight;
		Input::setWindowSize(glm::vec2((float)newWidth, (float)newHeight));
		glViewport(0, 0, newWidth, newHeight);
	}

	void Window::init()
	{
		glfwInit();
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_SAMPLES, 4);
	}

	Window* Window::create(const char* title)
	{
		Window* res = new Window();

		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		if (!monitor)
		{
			g_logger_error("Failed to get primary monitor.");
			return nullptr;
		}
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		if (!mode)
		{
			g_logger_error("Failed to get video mode of primary monitor.");
			return nullptr;
		}
		g_logger_info("Montior size: %d, %d", mode->width, mode->height);
		
		// The smallest monitor size we accept is 300x200
		res->width = glm::clamp(mode->width / 2, 300, INT_MAX);
		res->height = glm::clamp(mode->height / 2, 200, INT_MAX);
		res->title = title;
		Input::setWindowSize(glm::vec2((float)res->width, (float)res->height));

		res->windowPtr = (void*)glfwCreateWindow(res->width, res->height, title, nullptr, nullptr);
		if (res->windowPtr == nullptr)
		{
			glfwTerminate();
			g_logger_error("Failed to create window.");
			return res;
		}
		g_logger_info("GLFW window created");

		glfwSetWindowUserPointer((GLFWwindow*)res->windowPtr, (void*)(res));
		res->makeContextCurrent();

		glfwSetCursorPosCallback((GLFWwindow*)res->windowPtr, Input::mouseCallback);
		glfwSetKeyCallback((GLFWwindow*)res->windowPtr, Input::keyCallback);
		glfwSetFramebufferSizeCallback((GLFWwindow*)res->windowPtr, resizeCallback);
		glfwSetMouseButtonCallback((GLFWwindow*)res->windowPtr, Input::mouseButtonCallback);
		glfwSetCharCallback((GLFWwindow*)res->windowPtr, Input::charCallback);
		glfwSetScrollCallback((GLFWwindow*)res->windowPtr, Input::scrollCallback);

		int monitorX, monitorY;
		glfwGetMonitorPos(monitor, &monitorX, &monitorY);

		int windowWidth, windowHeight;
		glfwGetWindowSize((GLFWwindow*)res->windowPtr, &windowWidth, &windowHeight);

		glfwSetWindowPos((GLFWwindow*)res->windowPtr,
			monitorX + (mode->width - windowWidth) / 2,
			monitorY + (mode->height - windowHeight) / 2);

		res->setVSync(true);

		return res;
	}

	void Window::setTitle(const char* title)
	{
		glfwSetWindowTitle((GLFWwindow*)windowPtr, title);
	}

	void Window::setSize(int width, int height)
	{
		glfwSetWindowSize((GLFWwindow*)windowPtr, width, height);
	}

	void Window::setCursorMode(CursorMode cursorMode)
	{
		int glfwCursorMode =
			cursorMode == CursorMode::Locked ? GLFW_CURSOR_DISABLED :
			cursorMode == CursorMode::Normal ? GLFW_CURSOR_NORMAL :
			cursorMode == CursorMode::Hidden ? GLFW_CURSOR_HIDDEN :
			GLFW_CURSOR_HIDDEN;

		glfwSetInputMode((GLFWwindow*)windowPtr, GLFW_CURSOR, glfwCursorMode);
	}

	void Window::makeContextCurrent()
	{
		glfwMakeContextCurrent((GLFWwindow*)windowPtr);
	}

	void Window::pollInput()
	{
		Input::endFrame();
		glfwPollEvents();
	}

	void Window::close()
	{
		glfwSetWindowShouldClose((GLFWwindow*)windowPtr, GLFW_TRUE);
	}

	void Window::destroy()
	{
		glfwDestroyWindow((GLFWwindow*)windowPtr);
		windowPtr = nullptr;
	}

	bool Window::shouldClose()
	{
		return glfwWindowShouldClose((GLFWwindow*)windowPtr);
	}

	void Window::swapBuffers()
	{
		glfwSwapBuffers((GLFWwindow*)windowPtr);
	}

	float Window::getAspectRatio() const
	{
		return (float)width / (float)height;
	}

	void Window::setVSync(bool on)
	{
		if (on)
		{
			glfwSwapInterval(1);
		}
		else
		{
			glfwSwapInterval(0);
		}
	}

	void Window::free()
	{
		// Clean up
		glfwTerminate();
	}
}
