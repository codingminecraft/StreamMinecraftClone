#ifndef MINECRAFT_TEXTURE_PACKER_H
#define MINECRAFT_TEXTURE_PACKER_H
#include "core.h"

namespace Minecraft
{
	struct Pixel
	{
		uint8 r, g, b, a;
	};

	namespace TexturePacker
	{
		void packTextures(const char* filepath, const char* configFilepath, const char* outputFilepath, const char* yamlKeyName, bool generateMips = false, int texWidth = 32, int texHeight = 32);
	}
}

#endif