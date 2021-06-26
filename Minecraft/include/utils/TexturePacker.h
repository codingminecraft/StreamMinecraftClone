#pragma once
#include "core.h"

namespace Minecraft
{
	struct Pixel
	{
		uint8 r, g, b, a;
	};

	namespace TexturePacker
	{
		void packTextures(const char* filepath);
	}
}