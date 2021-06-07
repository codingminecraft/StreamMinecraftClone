#ifndef MINECRAFT_TEXTURE_H
#define MINECRAFT_TEXTURE_H
#include "core.h"

namespace Minecraft
{
	enum class FilterMode
	{
		None = 0,
		Linear,
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
		RGBA,
		RGBA8,

		RGB,
		RGB8,

		R32UI,
		RED_INTEGER,

		// Depth/Stencil formats
		DEPTH24_STENCIL8
	};

	struct Texture
	{
		uint32 GraphicsId = (uint32)-1;
		int32 Width = 0;
		int32 Height = 0;

		// Texture attributes
		FilterMode MagFilter = FilterMode::None;
		FilterMode MinFilter = FilterMode::None;
		WrapMode WrapS = WrapMode::None;
		WrapMode WrapT = WrapMode::None;
		ByteFormat InternalFormat = ByteFormat::None;
		ByteFormat ExternalFormat = ByteFormat::None;

		std::filesystem::path Path = std::filesystem::path();
		bool IsDefault = false;
	};

	namespace TextureUtil
	{
		// Namespace variables
		// NOTE: To make sure this variable is visible to other translation units, declare it as extern
		extern const Texture NullTexture;

		void Bind(const Texture& texture);
		void Unbind(const Texture& texture);
		void Delete(Texture& texture);

		// Loads a texture using stb library and generates a texutre using the filter/wrap modes and automatically detects
		// internal/external format, width, height, and alpha channel
		void Generate(Texture& texture, const std::filesystem::path& filepath);

		// Allocates memory space on the GPU according to the texture specifications listed here
		void Generate(Texture& texture);

		bool IsNull(const Texture& texture);

		uint32 ToGl(ByteFormat format);
		uint32 ToGl(WrapMode wrapMode);
		uint32 ToGl(FilterMode filterMode);
		uint32 ToGlDataType(ByteFormat format);
		bool ByteFormatIsInt(ByteFormat format);
	}
}

#endif
