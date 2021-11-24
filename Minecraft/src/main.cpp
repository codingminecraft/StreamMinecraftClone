// UNCOMMENT ME FOR MEMORY LEAK DETECTION STUFF
//#include <crtdbg.h>
#include <cppUtils/cppUtils.hpp>
#include "core/Application.h"
#include "world/TerrainGenerator.h"

int main()
{
	//_CrtSetDbgFlag(_CRTDBG_CHECK_ALWAYS_DF);

	g_memory_init(true);
#ifdef _RELEASE
	g_logger_set_level(g_logger_level::Info);
#endif

	//Minecraft::TerrainGenerator::outputNoiseToTextures();
	//return 0;

	Minecraft::Application::init();
	Minecraft::Application::run();
	Minecraft::Application::free();
	
	g_memory_dumpMemoryLeaks();
	_CrtCheckMemory();
	//_CrtDumpMemoryLeaks();
	return 0;
}

