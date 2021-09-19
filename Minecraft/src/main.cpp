#include <cppUtils/cppUtils.hpp>
#include "core/Application.h"

int main()
{
	g_memory_init(true);
#ifdef _RELEASE
	g_logger_set_level(g_logger_level::Info);
#endif

	Minecraft::Application::init();
	Minecraft::Application::run();
	Minecraft::Application::free();

	g_memory_dumpMemoryLeaks();
	return 0;
}

