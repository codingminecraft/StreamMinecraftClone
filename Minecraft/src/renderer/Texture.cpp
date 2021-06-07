#include "renderer/Texture.h"

#include <stb/stb_image.h>

namespace Minecraft
{
	namespace TextureUtil
	{
		const Texture NullTexture = {};

		uint32 ToGl(WrapMode wrapMode)
		{
			switch (wrapMode)
			{
			case WrapMode::Repeat:
				return GL_REPEAT;
			case WrapMode::None:
				return GL_NONE;
			default:
				Logger::Warning("Unknown glWrapMode '%d'", wrapMode);
			}

			return GL_NONE;
		}

		uint32 ToGl(FilterMode filterMode)
		{
			switch (filterMode)
			{
			case FilterMode::Linear:
				return GL_LINEAR;
			case FilterMode::Nearest:
				return GL_NEAREST;
			case FilterMode::None:
				return GL_NONE;
			default:
				Logger::Warning("Unknown glFilterMode '%d'", filterMode);
			}

			return GL_NONE;
		}

		uint32 ToGl(ByteFormat format)
		{
			switch (format)
			{
			case ByteFormat::RGBA8:
				return GL_RGBA8;
			case ByteFormat::RGB8:
				return GL_RGB8;
			case ByteFormat::RGBA:
				return GL_RGBA;
			case ByteFormat::RGB:
				return GL_RGB;
			case ByteFormat::R32UI:
				return GL_R32UI;
			case ByteFormat::RED_INTEGER:
				return GL_RED_INTEGER;
			case ByteFormat::DEPTH24_STENCIL8:
				return GL_DEPTH24_STENCIL8;
			case ByteFormat::None:
				return GL_NONE;
			default:
				Logger::Warning("Unknown glByteFormat '%d'", format);
			}

			return GL_NONE;
		}

		uint32 ToGlDataType(ByteFormat format)
		{
			switch (format)
			{
			case ByteFormat::RGBA8:
				return GL_FLOAT;
			case ByteFormat::RGB8:
				return GL_FLOAT;
			case ByteFormat::RGBA:
				return GL_FLOAT;
			case ByteFormat::RGB:
				return GL_FLOAT;
			case ByteFormat::R32UI:
				return GL_UNSIGNED_INT;
			case ByteFormat::RED_INTEGER:
				return GL_UNSIGNED_INT;
			case ByteFormat::None:
				return GL_NONE;
			default:
				Logger::Warning("Unknown glByteFormat '%d'", format);
			}

			return GL_NONE;
		}

		bool ByteFormatIsInt(ByteFormat format)
		{
			switch (format)
			{
			case ByteFormat::RGBA8:
				return false;
			case ByteFormat::RGB8:
				return false;
			case ByteFormat::RGBA:
				return false;
			case ByteFormat::RGB:
				return false;
			case ByteFormat::R32UI:
				return true;
			case ByteFormat::RED_INTEGER:
				return true;
			case ByteFormat::None:
				return GL_NONE;
			default:
				Logger::Warning("Unknown glByteFormat '%d'", format);
			}

			return false;
		}

		static void BindTextureParameters(const Texture& texture)
		{
			if (texture.WrapS != WrapMode::None)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToGl(texture.WrapS));
			}
			if (texture.WrapT != WrapMode::None)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToGl(texture.WrapT));
			}
			if (texture.MinFilter != FilterMode::None)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, ToGl(texture.MinFilter));
			}
			if (texture.MagFilter != FilterMode::None)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, ToGl(texture.MagFilter));
			}
		}

		void Generate(Texture& texture, const std::filesystem::path& path)
		{
			int channels;

			unsigned char* pixels = stbi_load(path.string().c_str(), &texture.Width, &texture.Height, &channels, 0);
			Logger::Assert((pixels != nullptr), "STB failed to load image: %s\n-> STB Failure Reason: %s", path.string().c_str(), stbi_failure_reason());

			int bytesPerPixel = channels;
			if (bytesPerPixel == 4)
			{
				texture.InternalFormat = ByteFormat::RGBA8;
				texture.ExternalFormat = ByteFormat::RGBA;
			}
			else if (bytesPerPixel == 3)
			{
				texture.InternalFormat = ByteFormat::RGB8;
				texture.ExternalFormat = ByteFormat::RGB;
			}
			else
			{
				Logger::Warning("Unknown number of channels '%d' in image '%s'.", path.string().c_str(), channels);
				return;
			}

			glGenTextures(1, &texture.GraphicsId);
			glBindTexture(GL_TEXTURE_2D, texture.GraphicsId);

			BindTextureParameters(texture);

			uint32 internalFormat = ToGl(texture.InternalFormat);
			uint32 externalFormat = ToGl(texture.ExternalFormat);
			Logger::Assert(internalFormat != GL_NONE && externalFormat != GL_NONE, "Tried to load image from file, but failed to identify internal format for image '%s'", texture.Path.string().c_str());
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, texture.Width, texture.Height, 0, externalFormat, GL_UNSIGNED_BYTE, pixels);

			stbi_image_free(pixels);
		}

		void Generate(Texture& texture)
		{
			Logger::Assert(texture.InternalFormat != ByteFormat::None, "Cannot generate texture without internal format.");
			Logger::Assert(texture.ExternalFormat != ByteFormat::None, "Cannot generate texture without external format.");
			glGenTextures(1, &texture.GraphicsId);
			glBindTexture(GL_TEXTURE_2D, texture.GraphicsId);

			BindTextureParameters(texture);

			uint32 internalFormat = ToGl(texture.InternalFormat);
			uint32 externalFormat = ToGl(texture.ExternalFormat);

			// Here the GL_UNSIGNED_BYTE does nothing since we are just allocating space
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, texture.Width, texture.Height, 0, externalFormat, GL_UNSIGNED_BYTE, nullptr);
		}

		bool IsNull(const Texture& texture)
		{
			return texture.GraphicsId == NullTexture.GraphicsId;
		}

		void Bind(const Texture& texture)
		{
			glBindTexture(GL_TEXTURE_2D, texture.GraphicsId);
		}

		void Unbind(const Texture& texture)
		{
			glBindTexture(GL_TEXTURE_2D, texture.GraphicsId);
		}

		void Delete(Texture& texture)
		{
			glDeleteTextures(1, &texture.GraphicsId);
			texture.GraphicsId = -1;
		}
	}
}