#include "renderer/Texture.h"
#include "core/File.h"

namespace Minecraft
{
	static const uint32 NULL_TEXTURE_ID = UINT32_MAX;

	static void bindTextureParameters(const Texture& texture);

	// ========================================================
	// 	   Texture Builder
	// ========================================================
	TextureBuilder::TextureBuilder()
	{
		// Set default values for a texture
		texture.type = TextureType::_2D;
		texture.minFilter = FilterMode::Linear;
		texture.magFilter = FilterMode::Linear;
		texture.wrapS = WrapMode::None;
		texture.wrapT = WrapMode::None;
		texture.graphicsId = NULL_TEXTURE_ID;
		texture.width = 0;
		texture.height = 0;
		texture.format = ByteFormat::None;
		texture.path = (char*)"";
		texture.swizzleFormat[0] = ColorChannel::Red;
		texture.swizzleFormat[1] = ColorChannel::Green;
		texture.swizzleFormat[2] = ColorChannel::Blue;
		texture.swizzleFormat[3] = ColorChannel::Alpha;
		texture.generateMipmap = false;
	}

	TextureBuilder::TextureBuilder(const Texture& texture)
	{
		this->texture = texture;
	}

	TextureBuilder& TextureBuilder::setMagFilter(FilterMode mode)
	{
		texture.magFilter = mode;
		return *this;
	}

	TextureBuilder& TextureBuilder::setMinFilter(FilterMode mode)
	{
		texture.minFilter = mode;
		return *this;
	}

	TextureBuilder& TextureBuilder::setWrapS(WrapMode mode)
	{
		texture.wrapS = mode;
		return *this;
	}

	TextureBuilder& TextureBuilder::setWrapT(WrapMode mode)
	{
		texture.wrapT = mode;
		return *this;
	}

	TextureBuilder& TextureBuilder::setFormat(ByteFormat format)
	{
		texture.format = format;
		return *this;
	}

	TextureBuilder& TextureBuilder::setFilepath(const char* filepath)
	{
		int strLength = (int)std::strlen(filepath);
		g_logger_assert(strLength > 0, "Bad filepath?");
		texture.path = (char*)g_memory_allocate(sizeof(char) * (strLength + 1));
		std::strcpy(texture.path, filepath);
		texture.path[strLength] = '\0';
		return *this;
	}

	TextureBuilder& TextureBuilder::setWidth(uint32 width)
	{
		texture.width = width;
		return *this;
	}

	TextureBuilder& TextureBuilder::setHeight(uint32 height)
	{
		texture.height = height;
		return *this;
	}

	TextureBuilder& TextureBuilder::setSwizzle(std::initializer_list<ColorChannel> swizzleMask)
	{
		g_logger_assert(swizzleMask.size() == 4, "Must set swizzle mask to { R, G, B, A } format. Size must be 4.");
		texture.swizzleFormat[0] = *(swizzleMask.begin() + 0);
		texture.swizzleFormat[1] = *(swizzleMask.begin() + 1);
		texture.swizzleFormat[2] = *(swizzleMask.begin() + 2);
		texture.swizzleFormat[3] = *(swizzleMask.begin() + 3);
		return *this;
	}

	TextureBuilder& TextureBuilder::setTextureType(TextureType type)
	{
		texture.type = type;
		return *this;
	}

	TextureBuilder& TextureBuilder::generateTextureObject()
	{
		glGenTextures(1, &texture.graphicsId);
		return *this;
	}

	TextureBuilder& TextureBuilder::setTextureObject(uint32 textureObjectId)
	{
		texture.graphicsId = textureObjectId;
		return *this;
	}

	TextureBuilder& TextureBuilder::generateMipmap()
	{
		texture.generateMipmap = true;
		return *this;
	}

	TextureBuilder& TextureBuilder::generateMipmapFromFile()
	{
		texture.generateMipmapFromFile = true;
		return *this;
	}

	TextureBuilder& TextureBuilder::bindTextureObject()
	{
		glBindTexture(TextureUtil::toGlType(texture.type), texture.graphicsId);
		return *this;
	}

	Texture TextureBuilder::generate(bool generateFromFilepath)
	{
		if (generateFromFilepath)
		{
			TextureUtil::generateFromFile(texture);
		}
		else
		{
			TextureUtil::generateEmptyTexture(texture);
		}

		return texture;
	}

	Texture TextureBuilder::build()
	{
		return texture;
	}

	// ========================================================
	// 	   Texture Member Functions
	// ========================================================
	void Texture::bind() const
	{
		glBindTexture(GL_TEXTURE_2D, graphicsId);
	}

	void Texture::unbind() const
	{
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	void Texture::destroy()
	{
		if (graphicsId != NULL_TEXTURE_ID)
		{
			glDeleteTextures(1, &graphicsId);
			graphicsId = NULL_TEXTURE_ID;
		}

		if (path != nullptr && std::strlen(path) > 0)
		{
			g_memory_free(path);
			path = nullptr;
		}
	}

	void Texture::uploadSubImage(int offsetX, int offsetY, int width, int height, uint8* buffer) const
	{
		g_logger_assert(format != ByteFormat::None, "Cannot generate texture without color format.");

		uint32 externalFormat = TextureUtil::toGlExternalFormat(format);
		uint32 dataType = TextureUtil::toGlDataType(format);

		switch (type)
		{
		case TextureType::_1D:
			glTexSubImage1D(GL_TEXTURE_1D, 0, offsetX, width, externalFormat, dataType, buffer);
			break;
		case TextureType::_2D:
			glTexSubImage2D(GL_TEXTURE_2D, 0, offsetX, offsetY, width, height, externalFormat, dataType, buffer);
			break;
		default:
			g_logger_error("Invalid texture type '%d'.", type);
		}
	}

	bool Texture::isNull() const
	{
		return graphicsId == NULL_TEXTURE_ID;
	}

	// ========================================================
	// 	   Texture Utilities
	// ========================================================
	namespace TextureUtil
	{
		uint32 toGl(WrapMode wrapMode)
		{
			switch (wrapMode)
			{
			case WrapMode::Repeat:
				return GL_REPEAT;
			case WrapMode::None:
				return GL_NONE;
			default:
				g_logger_warning("Unknown glWrapMode '%d'", wrapMode);
			}

			return GL_NONE;
		}

		uint32 toGl(FilterMode filterMode)
		{
			switch (filterMode)
			{
			case FilterMode::Linear:
				return GL_LINEAR;
			case FilterMode::Nearest:
				return GL_NEAREST;
			case FilterMode::LinearMipmapLinear:
				return GL_LINEAR_MIPMAP_LINEAR;
			case FilterMode::NearestMipmapNearest:
				return GL_NEAREST_MIPMAP_NEAREST;
			case FilterMode::LinearMipmapNearest:
				return GL_LINEAR_MIPMAP_NEAREST;
			case FilterMode::NearestMipmapLinear:
				return GL_NEAREST_MIPMAP_LINEAR;
			case FilterMode::None:
				return GL_NONE;
			default:
				g_logger_warning("Unknown glFilterMode '%d'", filterMode);
			}

			return GL_NONE;
		}

		uint32 toGlSizedInternalFormat(ByteFormat format)
		{
			switch (format)
			{
			case ByteFormat::RGBA8_UI:
				return GL_RGBA8;
			case ByteFormat::RGB8_UI:
				return GL_RGB8;
			case ByteFormat::RGBA_16F:
				return GL_RGBA16F;
			case ByteFormat::R32_UI:
				return GL_R32UI;
			case ByteFormat::R8_UI:
				return GL_R8UI;
			case ByteFormat::R8_F:
				return GL_R8;
			case ByteFormat::R32_F:
				return GL_R32F;
			case ByteFormat::None:
				return GL_NONE;
			case ByteFormat::ALPHA_F:
				return GL_ALPHA;
			case ByteFormat::DepthStencil:
				return GL_DEPTH24_STENCIL8;
			default:
				g_logger_warning("Unknown glByteFormat '%d'", format);
			}

			return GL_NONE;
		}

		uint32 toGlExternalFormat(ByteFormat format)
		{
			switch (format)
			{
			case ByteFormat::RGBA8_UI:
				return GL_RGBA;
			case ByteFormat::RGB8_UI:
				return GL_RGB;
			case ByteFormat::RGBA_16F:
				return GL_RGBA;
			case ByteFormat::R32_UI:
				return GL_RED_INTEGER;
			case ByteFormat::R8_UI:
				return GL_RED_INTEGER;
			case ByteFormat::R8_F:
				return GL_RED;
			case ByteFormat::R32_F:
				return GL_RED;
			case ByteFormat::ALPHA_F:
				return GL_ALPHA;
			case ByteFormat::None:
				return GL_NONE;
			case ByteFormat::DepthStencil:
				return GL_UNSIGNED_INT_24_8;
			default:
				g_logger_warning("Unknown glByteFormat '%d'", format);
			}

			return GL_NONE;
		}

		uint32 toGlDataType(ByteFormat format)
		{
			switch (format)
			{
			case ByteFormat::RGBA8_UI:
				return GL_UNSIGNED_BYTE;
			case ByteFormat::RGB8_UI:
				return GL_UNSIGNED_BYTE;
			case ByteFormat::RGBA_16F:
				return GL_HALF_FLOAT;
			case ByteFormat::R32_UI:
				return GL_UNSIGNED_INT;
			case ByteFormat::R8_UI:
				return GL_UNSIGNED_BYTE;
			case ByteFormat::R8_F:
				return GL_FLOAT;
			case ByteFormat::R32_F:
				return GL_FLOAT;
			case ByteFormat::ALPHA_F:
				return GL_FLOAT;
			case ByteFormat::None:
				return GL_NONE;
			default:
				g_logger_warning("Unknown glByteFormat '%d'", format);
			}

			return GL_NONE;
		}

		bool byteFormatIsInt(const Texture& texture)
		{
			switch (texture.format)
			{
			case ByteFormat::RGBA8_UI:
				return false;
			case ByteFormat::RGB8_UI:
				return false;
			case ByteFormat::R32_UI:
				return true;
			case ByteFormat::R8_UI:
				return true;
			case ByteFormat::ALPHA_F:
				return false;
			case ByteFormat::None:
				return false;
			default:
				g_logger_warning("Unknown glByteFormat '%d'", texture.format);
			}

			return false;

		}

		bool byteFormatIsRgb(const Texture& texture)
		{
			switch (texture.format)
			{
			case ByteFormat::RGBA8_UI:
				return true;
			case ByteFormat::RGB8_UI:
				return true;
			case ByteFormat::R32_UI:
				return false;
			case ByteFormat::R8_UI:
				return false;
			case ByteFormat::ALPHA_F:
				return false;
			case ByteFormat::None:
				return false;
			default:
				g_logger_warning("Unknown glByteFormat '%d'", texture.format);
			}

			return false;
		}

		uint32 toGlType(TextureType type)
		{
			switch (type)
			{
			case TextureType::_1D:
				return GL_TEXTURE_1D;
			case TextureType::_2D:
				return GL_TEXTURE_2D;
			case TextureType::_CUBEMAP:
				return GL_TEXTURE_CUBE_MAP;
			case TextureType::_CUBEMAP_POSITIVE_X:
			case TextureType::_CUBEMAP_NEGATIVE_X:
			case TextureType::_CUBEMAP_POSITIVE_Y:
			case TextureType::_CUBEMAP_NEGATIVE_Y:
			case TextureType::_CUBEMAP_POSITIVE_Z:
			case TextureType::_CUBEMAP_NEGATIVE_Z:
				return GL_TEXTURE_CUBE_MAP_POSITIVE_X + ((int)type - (int)TextureType::_CUBEMAP_POSITIVE_X);

			}

			g_logger_warning("Unknown texture type.");
			return GL_NONE;
		}

		int32 toGlSwizzle(ColorChannel colorChannel)
		{
			switch (colorChannel)
			{
			case ColorChannel::Blue:
				return GL_BLUE;
			case ColorChannel::Green:
				return GL_GREEN;
			case ColorChannel::Red:
				return GL_RED;
			case ColorChannel::One:
				return GL_ONE;
			case ColorChannel::Zero:
				return GL_ZERO;
			case ColorChannel::Alpha:
				return GL_ALPHA;
			default:
				g_logger_warning("Unknown glColorChannel swizzle format '%d'. Defaulting to 1.", colorChannel);
			}

			return GL_ONE;
		}

		void generateFromFile(Texture& texture)
		{
			g_logger_assert(texture.path != "", "Cannot generate texture from file without a filepath provided.");
			int channels;

			unsigned char* pixels = stbi_load(texture.path, &texture.width, &texture.height, &channels, 0);
			g_logger_assert((pixels != nullptr), "STB failed to load image: %s\n-> STB Failure Reason: %s", texture.path, stbi_failure_reason());

			int bytesPerPixel = channels;
			if (bytesPerPixel == 4)
			{
				texture.format = ByteFormat::RGBA8_UI;
			}
			else if (bytesPerPixel == 3)
			{
				texture.format = ByteFormat::RGB8_UI;
			}
			else
			{
				g_logger_warning("Unknown number of channels '%d' in image '%s'.", texture.path, channels);
				return;
			}

			bindTextureParameters(texture);

			uint32 internalFormat = TextureUtil::toGlSizedInternalFormat(texture.format);
			uint32 externalFormat = TextureUtil::toGlExternalFormat(texture.format);
			g_logger_assert(internalFormat != GL_NONE && externalFormat != GL_NONE, "Tried to load image from file, but failed to identify internal format for image '%s'", texture.path);
			uint32 textureType = TextureUtil::toGlType(texture.type);
			switch (texture.type)
			{
			case TextureType::_1D:
				g_logger_error("Cannot use 1D texture with stb.");
				break;
			case TextureType::_2D:
			case TextureType::_CUBEMAP_POSITIVE_X:
			case TextureType::_CUBEMAP_NEGATIVE_X:
			case TextureType::_CUBEMAP_POSITIVE_Y:
			case TextureType::_CUBEMAP_NEGATIVE_Y:
			case TextureType::_CUBEMAP_POSITIVE_Z:
			case TextureType::_CUBEMAP_NEGATIVE_Z:
				glTexImage2D(textureType, 0, internalFormat, texture.width, texture.height, 0, externalFormat, GL_UNSIGNED_BYTE, pixels);
				break;
			default:
				g_logger_error("Invalid texture type '%d'.", texture.type);
			}

			if (texture.generateMipmap)
			{
				glGenerateMipmap(textureType);
			}

			if (texture.generateMipmapFromFile)
			{
				for (int i = 0; i < 40; i++)
				{
					std::string mipFile = std::string(texture.path) + ".mip." + std::to_string(i + 1) + ".png";

					if (!File::isFile(mipFile.c_str()))
					{
						break;
					}

					int width, height;
					unsigned char* mipPixels = stbi_load(texture.path, &width, &height, &channels, 0);
					glTexImage2D(textureType, i + 1, internalFormat, width, height, 0, externalFormat, GL_UNSIGNED_BYTE, mipPixels);
					stbi_image_free(mipPixels);
				}
			}

			stbi_image_free(pixels);
		}


		void generateEmptyTexture(Texture& texture)
		{
			g_logger_assert(texture.format != ByteFormat::None, "Cannot generate texture without color format.");
			glGenTextures(1, &texture.graphicsId);
			glBindTexture(GL_TEXTURE_2D, texture.graphicsId);

			bindTextureParameters(texture);

			uint32 internalFormat = TextureUtil::toGlSizedInternalFormat(texture.format);
			uint32 externalFormat = TextureUtil::toGlExternalFormat(texture.format);
			uint32 dataType = TextureUtil::toGlDataType(texture.format);
			uint32 textureType = TextureUtil::toGlType(texture.type);

			// Here the GL_UNSIGNED_BYTE does nothing since we are just allocating space
			switch (texture.type)
			{
			case TextureType::_1D:
				glTexImage1D(textureType, 0, internalFormat, texture.width, 0, externalFormat, dataType, nullptr);
				break;
			case TextureType::_2D:
				glTexImage2D(textureType, 0, internalFormat, texture.width, texture.height, 0, externalFormat, dataType, nullptr);
				break;
			case TextureType::_CUBEMAP_NEGATIVE_X:
			case TextureType::_CUBEMAP_POSITIVE_X:
			case TextureType::_CUBEMAP_NEGATIVE_Y:
			case TextureType::_CUBEMAP_POSITIVE_Y:
			case TextureType::_CUBEMAP_NEGATIVE_Z:
			case TextureType::_CUBEMAP_POSITIVE_Z:
				glTexImage2D(textureType, 0, internalFormat, texture.width, texture.height, 0, externalFormat, dataType, nullptr);
				break;
			default:
				g_logger_error("Invalid texture type '%d'.", texture.type);
			}
		}
	}

	// ========================================================
	// 	   Internal helper functions
	// ========================================================
	static void bindTextureParameters(const Texture& texture)
	{
		uint32 type = TextureUtil::toGlType(texture.type);
		if (texture.wrapS != WrapMode::None)
		{
			glTexParameteri(type, GL_TEXTURE_WRAP_S, TextureUtil::toGl(texture.wrapS));
		}
		if (texture.wrapT != WrapMode::None)
		{
			glTexParameteri(type, GL_TEXTURE_WRAP_T, TextureUtil::toGl(texture.wrapT));
		}
		if (texture.minFilter != FilterMode::None)
		{
			glTexParameteri(type, GL_TEXTURE_MIN_FILTER, TextureUtil::toGl(texture.minFilter));
		}
		if (texture.magFilter != FilterMode::None)
		{
			glTexParameteri(type, GL_TEXTURE_MAG_FILTER, TextureUtil::toGl(texture.magFilter));
		}

		//GLint swizzleMask[4] = {
		//	TextureUtil::toGlSwizzle(texture.swizzleFormat[0]),
		//	TextureUtil::toGlSwizzle(texture.swizzleFormat[1]),
		//	TextureUtil::toGlSwizzle(texture.swizzleFormat[2]),
		//	TextureUtil::toGlSwizzle(texture.swizzleFormat[3])
		//};
		//glTexParameteriv(type, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
	}
}