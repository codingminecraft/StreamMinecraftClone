// Implementations
// TODO: Remove all includes in headers
// TODO: Only allowed to include <stdint> and <stdbool> in headers
#define GABE_CPP_UTILS_IMPL
#include <CppUtils/CppUtils.h>
#undef GABE_CPP_UTILS_IMPL
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#undef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_write.h>
#undef STB_IMAGE_WRITE_IMPLEMENTATION

#include <CppUtils/CppUtils.h>
#include "utils/ErrorCodes.h"
#include "world/World.h"

int main()
{
	Memory::init();

	int error = Minecraft::World::playGame();
	if (!error)
	{
		return 0;
	}


	switch (error)
	{
	case Minecraft::ErrorCodes::GLFW_WINDOW_CREATION_FAILED:
		CppUtils::Logger::Error("Failed to create window with GLFW.");
		break;
	case Minecraft::ErrorCodes::GLAD_INITIALIZATION_FAILED:
		CppUtils::Logger::Error("Failed to initialize GLAD.");
		break;
	}
	CppUtils::Logger::Error("Game crashed unexpectedly. TODO Print error report here.");
	return -1;
}

