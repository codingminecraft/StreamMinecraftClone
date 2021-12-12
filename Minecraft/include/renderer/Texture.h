#ifndef MINECRAFT_TEXTURE_H
#define MINECRAFT_TEXTURE_H
#include "core.h"

namespace Minecraft
{
	enum class FilterMode
	{
		None = 0,
		Linear,
		LinearMipmapLinear,
		NearestMipmapNearest,
		LinearMipmapNearest,
		NearestMipmapLinear,
		Nearest
	};

	enum class WrapMode
	{
		None = 0,
		Repeat
	};

	enum class ByteFormat
	{
		None = 0,
		RGBA8_UI,
		RGBA_16F,

		RGB8_UI,

		R32_UI,
		R32_F,
		R8_UI,
		R8_F,

		ALPHA_F,

		DepthStencil
	};

	enum class ColorChannel
	{
		None = 0,
		Red,
		Green,
		Blue,
		Alpha,
		Zero,
		One
	};

	enum class TextureType
	{
		None = 0,
		_1D,
		_2D,
		_CUBEMAP,
		_CUBEMAP_POSITIVE_X,
		_CUBEMAP_NEGATIVE_X,
		_CUBEMAP_POSITIVE_Y,
		_CUBEMAP_NEGATIVE_Y,
		_CUBEMAP_POSITIVE_Z,
		_CUBEMAP_NEGATIVE_Z
	};

	struct Texture
	{
		TextureType type;
		uint32 graphicsId;
		// TODO: Change these to uint32
		int32 width;
		int32 height;

		// Texture attributes
		FilterMode magFilter;
		FilterMode minFilter;
		WrapMode wrapS;
		WrapMode wrapT;
		ByteFormat format;
		ColorChannel swizzleFormat[4];
		bool generateMipmap;
		bool generateMipmapFromFile;

		char* path;

		void bind() const;
		void unbind() const;
		void destroy();

		void uploadSubImage(int offsetX, int offsetY, int width, int height, uint8* buffer) const;

		bool isNull() const;
	};

	class TextureBuilder
	{
	public:
		TextureBuilder();
		TextureBuilder(const Texture& texture);

		TextureBuilder& setMagFilter(FilterMode mode);
		TextureBuilder& setMinFilter(FilterMode mode);
		TextureBuilder& setWrapS(WrapMode mode);
		TextureBuilder& setWrapT(WrapMode mode);
		TextureBuilder& setFormat(ByteFormat format);
		TextureBuilder& setFilepath(const char* filepath);
		TextureBuilder& setWidth(uint32 width);
		TextureBuilder& setHeight(uint32 height);
		TextureBuilder& setSwizzle(std::initializer_list<ColorChannel> swizzleMask);
		TextureBuilder& setTextureType(TextureType type);
		TextureBuilder& generateTextureObject();
		TextureBuilder& setTextureObject(uint32 textureObjectId);
		TextureBuilder& generateMipmap();
		TextureBuilder& generateMipmapFromFile();
		TextureBuilder& bindTextureObject();

		Texture generate(bool generateFromFilepath = false);
		Texture build();

	private:
		Texture texture;
	};

	namespace TextureUtil
	{
		uint32 toGlSizedInternalFormat(ByteFormat format);
		uint32 toGlExternalFormat(ByteFormat format);
		uint32 toGl(WrapMode wrapMode);
		uint32 toGl(FilterMode filterMode);
		uint32 toGlDataType(ByteFormat format);
		int32 toGlSwizzle(ColorChannel colorChannel);
		uint32 toGlType(TextureType type);

		bool byteFormatIsInt(const Texture& texture);
		bool byteFormatIsRgb(const Texture& texture);

		void generateFromFile(Texture& texture);
		void generateEmptyTexture(Texture& texture);
	}
}

#endif