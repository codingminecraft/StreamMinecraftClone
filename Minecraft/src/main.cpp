#include <cppUtils/cppUtils.hpp>
#include "core/Application.h"

int main()
{
	g_memory_init(true);
#ifdef _RELEASE
	g_logger_set_level(g_logger_level::Warning);
#endif

	Minecraft::Application::run();

	g_memory_dumpMemoryLeaks();
	return 0;
}

