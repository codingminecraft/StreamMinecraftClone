// Implementations
// TODO: Remove all includes in headers
// TODO: Only allowed to include <stdint> and <stdbool> in headers
#include <memory/memory.h>

#include "core/Application.h"

int main()
{
	g_memory_init(true);

	Minecraft::Application::run();

	g_memory_dumpMemoryLeaks();
	return 0;
}

