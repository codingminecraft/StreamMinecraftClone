#ifndef MINECRAFT_BLOCK_MAP_H
#define MINECRAFT_BLOCK_MAP_H
#include "core.h"
#include "Chunk.h"

namespace Minecraft
{
	struct Texture;

	struct BlockFormat
	{
		std::string sideTexture;
		std::string topTexture;
		std::string bottomTexture;
		bool isTransparent;
	};

	struct TextureFormat
	{
		// UV's are stored in bottom-right, top-right, top-left, bottom-left format
		glm::vec2 uvs[4];
		uint16 id;
	};

	namespace BlockMap
	{
		extern Block NULL_BLOCK;

		const TextureFormat& getTextureFormat(const std::string& textureName);
		const BlockFormat& getBlock(const std::string& name);
		const BlockFormat& getBlock(int blockId);
		void loadBlocks(const char* textureFormatConfig, const char* blockFormatConfig);
		void uploadTextureCoordinateMapToGpu();

		uint32 getTextureCoordinatesTextureId();
	}
}

#endif
