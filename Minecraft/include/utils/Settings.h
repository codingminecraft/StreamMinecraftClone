#ifndef MINECRAFT_SETTINGS_H
#define MINECRAFT_SETTINGS_H
#include "core.h"

namespace Minecraft
{
	namespace Settings
	{
		namespace Window
		{
			const int width = 1920;
			const int height = 1080;
			const glm::vec4 clearColor = "#99ccffff"_hex;
			const char* title = "Minecraft Clone";
		}
	}
}

#endif