#include "core.h"
#include "core/Window.h"
#include "core/Input.h"

namespace Minecraft
{
	static void resizeCallback(GLFWwindow* windowPtr, int newWidth, int newHeight)
	{
		Window* userWindow = (Window*)glfwGetWindowUserPointer(windowPtr);
		userWindow->width = newWidth;
		userWindow->height = newHeight;
		glViewport(0, 0, newWidth, newHeight);
	}

	void Window::init()
	{
		glfwInit();
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_SAMPLES, 16);
	}

	Window* Window::create(int width, int height, const char* title)
	{
		Window* res = new Window();
		res->width = width;
		res->height = height;
		res->title = title;

		res->windowPtr = (void*)glfwCreateWindow(width, height, title, nullptr, nullptr);
		if (res->windowPtr == nullptr)
		{
			glfwTerminate();
			g_logger_error("Window creation failed.");
			return nullptr;
		}
		g_logger_info("GLFW window created");

		glfwSetWindowUserPointer((GLFWwindow*)res->windowPtr, (void*)(&res));
		res->makeContextCurrent();

		glfwSetCursorPosCallback((GLFWwindow*)res->windowPtr, Input::mouseCallback);
		glfwSetKeyCallback((GLFWwindow*)res->windowPtr, Input::keyCallback);
		glfwSetFramebufferSizeCallback((GLFWwindow*)res->windowPtr, resizeCallback);

		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		int monitorX, monitorY;
		glfwGetMonitorPos(monitor, &monitorX, &monitorY);

		int windowWidth, windowHeight;
		glfwGetWindowSize((GLFWwindow*)res->windowPtr, &windowWidth, &windowHeight);

		glfwSetWindowPos((GLFWwindow*)res->windowPtr,
			monitorX + (mode->width - windowWidth) / 2,
			monitorY + (mode->height - windowHeight) / 2);

		return res;
	}

	void Window::free(Window* window)
	{
		delete window;
	}

	void Window::setTitle(const char* title)
	{
		glfwSetWindowTitle((GLFWwindow*)windowPtr, title);
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

	bool Window::shouldClose()
	{
		return glfwWindowShouldClose((GLFWwindow*)windowPtr);
	}

	void Window::swapBuffers()
	{
		glfwSwapBuffers((GLFWwindow*)windowPtr);
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

	void Window::update(float dt)
	{

	}

	void Window::cleanup()
	{
		// Clean up
		glfwTerminate();
	}
}
