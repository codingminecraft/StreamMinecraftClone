#ifndef MINECRAFT_BLOCK_MAP_H
#define MINECRAFT_BLOCK_MAP_H
#include "core.h"

namespace Minecraft
{
	struct Sprite;
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
		uint16 lightLevel;
		int16 lightColor;
		int16 padding;

		bool isLightSource() const;
		bool isTransparent() const;

		inline bool isLightPassable() const 
		{
			return isLightSource() || isTransparent();
		}

		inline void setLightLevel(int level)
		{
			// Mask out the lower 5 bits
			lightLevel &= 0x3e0;
			// Set the lower 5 bits to the new level
			lightLevel |= (level & 0x1f);
		}

		inline void setSkyLightLevel(int level)
		{
			// Mask out the 5-10 bits
			lightLevel &= 0x01f;
			// Set the 5-10 bits to the new level
			lightLevel |= ((level & 0x1f) << 5);
		}

		inline int calculatedLightLevel() const
		{
			return lightLevel & 0x1f;
		}

		inline int calculatedSkyLightLevel() const
		{
			return (lightLevel & 0x3e0) >> 5;
		}
	};

	bool operator==(const Block& a, const Block& b);
	bool operator!=(const Block& a, const Block& b);

	struct TextureFormat;
	struct BlockFormat
	{
		TextureFormat* sideTexture;
		TextureFormat* topTexture;
		TextureFormat* bottomTexture;
		std::string itemPictureName;
		bool isTransparent;
		bool isSolid;
		bool colorTopByBiome;
		bool colorSideByBiome;
		bool colorBottomByBiome;
		bool isBlendable;
		bool isLightSource;
		int lightLevel;
		// TODO: Add bounding box definition here. That way we can add custom bounding boxes for different blocks
	};

	struct TextureFormat
	{
		// UV's are stored in bottom-right, top-right, top-left, bottom-left format
		glm::vec2 uvs[4];
		uint16 id;
		const Texture* texture;
	};

	namespace BlockMap
	{
		extern Block NULL_BLOCK;
		extern Block AIR_BLOCK;

		const TextureFormat& getTextureFormat(const std::string& textureName);
		const BlockFormat& getBlock(const std::string& name);
		const int getBlockId(const std::string& name);
		const BlockFormat& getBlock(int blockId);
		void loadBlocks(const char* textureFormatConfig, const char* itemFormatConfig, const char* blockFormatConfig);
		void loadBlockItemTextures(const char* blockFormatConfig);
		void uploadTextureCoordinateMapToGpu();
		void patchTextureMaps(const Texture* blockTexture, const Texture* itemTexture);
		void patchBlockItemTextureMaps(const Texture* blockItemTexture);
		void generateBlockItemPictures(const char* blockFormatConfig, const char* outputPath);

		uint32 getTextureCoordinatesTextureId();
	}
}

#endif
