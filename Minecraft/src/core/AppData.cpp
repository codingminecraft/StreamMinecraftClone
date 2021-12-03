#include "core/AppData.h"
#include "core/File.h"

namespace Minecraft
{
	namespace AppData
	{
		std::string appDataFilepath = "";
		std::string worldsRootPath = "";
		std::string screenshotsPath = "";

		void init()
		{
			// Initialize and create any filepaths for save information
			appDataFilepath = (std::filesystem::path(File::getSpecialAppFolder()) / std::filesystem::path(".minecraftClone")).string();
			File::createDirIfNotExists(appDataFilepath.c_str());
			g_logger_info("Minecraft Clone directory: %s", appDataFilepath.c_str());
			worldsRootPath = (std::filesystem::path(appDataFilepath) / std::filesystem::path("worlds")).string();
			File::createDirIfNotExists(worldsRootPath.c_str());
			screenshotsPath = (std::filesystem::path(appDataFilepath) / std::filesystem::path("screenshots")).string();
			File::createDirIfNotExists(screenshotsPath.c_str());
		}
	}
}