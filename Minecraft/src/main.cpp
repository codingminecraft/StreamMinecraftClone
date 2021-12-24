// UNCOMMENT ME FOR MEMORY LEAK DETECTION STUFF
//#include <crtdbg.h>
#include <cppUtils/cppUtils.hpp>
#include "core/Application.h"
#include "world/TerrainGenerator.h"
#include "core/GlobalThreadPool.h"

using namespace Minecraft;

int main()
{
	//_CrtSetDbgFlag(_CRTDBG_CHECK_ALWAYS_DF);

#ifdef _DEBUG
	g_memory_init(true, 1024);
#elif defined(_RELEASE)
	//g_memory_init(false);
	g_logger_set_level(g_logger_level::Info);
#endif

	Minecraft::Application::init();
	Minecraft::Application::run();
	Minecraft::Application::free();

	g_memory_dumpMemoryLeaks();
#ifdef _WIN32
	//_CrtCheckMemory();
	//_CrtDumpMemoryLeaks();
#endif
	return 0;
}

