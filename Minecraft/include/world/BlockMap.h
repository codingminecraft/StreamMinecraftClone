#ifndef MINECRAFT_BLOCK_MAP_H
#define MINECRAFT_BLOCK_MAP_H
#include "core.h"

namespace Minecraft
{
	struct BlockFormat
	{
		std::string sideTexture;
		std::string topTexture;
		std::string bottomTexture;
		bool isTransparent;
	};

	struct TextureFormat
	{
		glm::vec2 uvs[4];
	};

	namespace BlockMap
	{
		extern int NULL_BLOCK;

		const TextureFormat& getTextureFormat(const std::string& textureName);
		const BlockFormat& getBlock(const std::string& name);
		const BlockFormat& getBlock(int blockId);
		void loadBlocks(const char* textureFormatConfig, const char* blockFormatConfig);
	}
}

#endif
