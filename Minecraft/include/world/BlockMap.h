#ifndef MINECRAFT_BLOCK_MAP_H
#define MINECRAFT_BLOCK_MAP_H
#include "core.h"

namespace Minecraft
{
	const uint16 NULL_BLOCK_ID = 0;
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
		uint16 id;
		uint16 lightLevel;
		int16 lightColor;

		// Bit 1 isTransparent
		// Bit 2 isBlendable
		// Bit 3 isLightSource
		int16 compressedData;

		bool isItemOnly() const;

		inline bool isTransparent() const
		{
			return (compressedData & 0b1);
		}

		inline void setTransparent(bool transparent)
		{
			compressedData |= transparent ? 0b1 : 0;
		}

		inline bool isBlendable() const
		{
			return (compressedData & 0b10);
		}

		inline void setIsBlendable(bool isBlendable)
		{
			compressedData |= isBlendable ? 0b10 : 0;
		}

		// TODO: This doesn't work for some reason
		inline void setIsLightSource(bool isLightSource)
		{
			compressedData |= isLightSource ? 0b100 : 0;
		}

		inline bool isLightSource() const
		{
			return (compressedData & 0b100);
		}

		//bool isLightSource() const;

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

		inline void setLightColor(const glm::ivec3& color)
		{
			// Convert from number between 0-255 to number between 0-7
			lightColor =
				(( ((int)((float)color.r / 255.0f) * 7) << 0) & 0x7)  | 
				(( ((int)((float)color.g / 255.0f) * 7) << 3) & 0x38) | 
				(( ((int)((float)color.b / 255.0f) * 7) << 6) & 0x1C0); 
		}

		inline glm::ivec3 getLightColor() const
		{
			// Convert from number between 0-7 to number between 0-255
			return glm::ivec3(
				(int)(((float)((lightColor & 0x7)   >> 0) / 7.0f) * 255.0f),  // R
				(int)(((float)((lightColor & 0x38)  >> 3) / 7.0f) * 255.0f),  // G
				(int)(((float)((lightColor & 0x1C0) >> 6) / 7.0f) * 255.0f)   // B
			);
		}

		inline glm::ivec3 getCompressedLightColor() const
		{
			return glm::ivec3(
				((lightColor & 0x7) >> 0),  // R
				((lightColor & 0x38) >> 3), // G
				((lightColor & 0x1C0) >> 6) // B
			);
		}

		inline bool isNull() const
		{
			return id == NULL_BLOCK_ID;
		}
	};

	inline bool operator==(const Block& a, const Block& b)
	{
		return a.id == b.id;
	}

	inline bool operator!=(const Block& a, const Block& b)
	{
		return a.id != b.id;
	}

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
		bool isItemOnly;
		bool isStackable;
		int maxStackCount;
		// TODO: Add bounding box definition here. That way we can add custom bounding boxes for different blocks
	};

	struct CraftingRecipe
	{
		int maxWidth;
		int maxHeight;
		// 9 is the max crafting size
		uint16 blockIds[9];
		int16 output;
		uint8 outputCount;
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
		void loadCraftingRecipes(const char* craftingRecipesConfig);
		void uploadTextureCoordinateMapToGpu();
		void patchTextureMaps(const Texture* blockTexture, const Texture* itemTexture);
		void patchBlockItemTextureMaps(const Texture* blockItemTexture);
		void generateBlockItemPictures(const char* blockFormatConfig, const char* outputPath);

		uint32 getTextureCoordinatesTextureId();

		const std::vector<CraftingRecipe>& getAllCraftingRecipes();
	}
}

#endif
