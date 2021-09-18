#include <stdio.h>

#define GABE_CPP_UTILS_IMPL
#include <cppUtils/cppUtils.hpp>

#include "File.h"
#include "Download.h"

const char* tmpDir = "./Minecraft/vendor/tmp";

const char* freetypeZipFile = "./Minecraft/vendor/tmp/freetypeTmp.zip";
const char* freetypeUnzipDir = "./Minecraft/vendor/tmp/freetypeUnzipped";
const char* freetypeVendorDir = "./Minecraft/vendor/freetype";
const char* freetypeUrl = "https://github.com/ubawurinna/freetype-windows-binaries/archive/refs/tags/v2.11.0.zip";

void install(const char* url, const char* zipFile, const char* unzipDir, const char* vendorDir, const char* unzippedFilename, zip_type zipType)
{
	if (manim_download(url, tmpDir, zipFile))
	{
		if (!manim_unzip(zipFile, unzipDir, zipType))
		{
			g_logger_error("Failed to unzip '%s'. Please install '%s' binaries manually. TODO: Write detailed instructions", zipFile, zipFile);
			return;
		}
	}

	if (manim_is_dir(vendorDir))
	{
		if (!manim_remove_dir(vendorDir))
		{
			g_logger_warning("Failed to remove directory for '%s'. Installation may fail.", vendorDir);
		}
	}

	if (!manim_move_file(unzippedFilename, vendorDir))
	{
		g_logger_error("Failed to move unzipped directory '%s' into '%s'", unzippedFilename, vendorDir);
	}
}

int main()
{
	install(freetypeUrl, freetypeZipFile, freetypeUnzipDir, freetypeVendorDir, "./Minecraft/vendor/tmp/freetypeUnzipped/freetype-windows-binaries-2.11.0", zip_type::_ZIP);

	g_logger_info("Removing tmp directory.");
	manim_remove_dir(tmpDir);

	return 0;
}