#ifndef MINECRAFT_APP_DATA_H
#define MINECRAFT_APP_DATA_H
#include "core.h"

namespace Minecraft
{
	namespace AppData
	{
		extern std::string appDataFilepath;
		extern std::string worldsRootPath;
		extern std::string screenshotsPath;

		void init();
	}
}

#endif 