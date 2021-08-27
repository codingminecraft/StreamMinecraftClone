// Implementations
// TODO: Remove all includes in headers
// TODO: Only allowed to include <stdint> and <stdbool> in headers
#include <memory/memory.h>
#include <logger/logger.h>

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

