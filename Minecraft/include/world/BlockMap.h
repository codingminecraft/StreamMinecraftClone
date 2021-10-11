#ifndef MINECRAFT_BLOCK_MAP_H
#define MINECRAFT_BLOCK_MAP_H
#include "core.h"

namespace Minecraft
{
	struct Texture;

	// 64 bits per block 
	// 16 bit integer id 2^16
	// 4 bits light level 0-15
	// 4 bits rotation direction 
	// 8 bits block type
	// 32 bits extra stuff
	struct Block
	{
		int16 id;
		int8 lightLevel;
		int8 rotation;
		int32 padding;
	};

	bool operator==(const Block& a, const Block& b);

	struct BlockFormat
	{
		std::string sideTexture;
		std::string topTexture;
		std::string bottomTexture;
		bool isTransparent;
		bool isSolid;
		// TODO: Add bounding box definition here. That way we can add custom bounding boxes for different blocks
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
		extern Block AIR_BLOCK;

		const TextureFormat& getTextureFormat(const std::string& textureName);
		const BlockFormat& getBlock(const std::string& name);
		const BlockFormat& getBlock(int blockId);
		void loadBlocks(const char* textureFormatConfig, const char* blockFormatConfig);
		void uploadTextureCoordinateMapToGpu();

		uint32 getTextureCoordinatesTextureId();
	}
}

#endif
