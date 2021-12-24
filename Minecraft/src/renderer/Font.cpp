#include "renderer/Font.h"

namespace Minecraft
{
	const RenderableChar& Font::getCharInfo(char c) const
	{
		auto iter = characterMap.find(c);
		if (iter == characterMap.end())
		{
			g_logger_warning("Font does not contain character code '%d'. Defaulting to empty glyph.", c);
			static RenderableChar emptyGlyph = {
				{0, 0},
				{0, 0}
			};
			return emptyGlyph;
		}
		return iter->second;
	}

	CharRange CharRange::Ascii = { 32, 127 };

	float Font::getKerning(char leftChar, char rightChar) const
	{
		FT_Vector kerning;

		FT_UInt leftGlyph = FT_Get_Char_Index(fontFace, leftChar);
		FT_UInt rightGlyph = FT_Get_Char_Index(fontFace, rightChar);
		int error = FT_Get_Kerning(fontFace, leftGlyph, rightGlyph, FT_Kerning_Mode::FT_KERNING_DEFAULT, &kerning);

		// Shift right 6 to divide by 64, since fonts are measured in 64ths of a pixel
		return (float)(kerning.x >> 6);
	}

	glm::vec2 Font::getSize(const std::string& str, float scale) const
	{
		float x = 0;
		float y = 0;
		float minY = 0;
		float maxY = 0;

		for (int i = 0; i < str.length(); i++)
		{
			char c = str[i];
			RenderableChar renderableChar = getCharInfo(c);
			float charWidth = renderableChar.charSize.x * scale;
			float charHeight = renderableChar.charSize.y * scale;
			float adjustedY = y - (renderableChar.charSize.y - renderableChar.bearingY) * scale;
			minY = glm::min(adjustedY, minY);
			maxY = glm::max(charHeight, maxY);

			char nextC = i < str.length() - 1 ? str[i + 1] : '\0';
			x += getKerning(c, nextC) * scale;
			x += renderableChar.advance.x * scale;
		}

		return glm::vec2(x, maxY - minY);
	}

	glm::vec2 Font::getVertSize(const std::string& str, float scale) const
	{
		// TODO: Implement me
		//float x = 0;
		//float y = 0;
		//float minY = 0;
		//float maxY = 0;

		//for (int i = 0; i < str.length(); i++)
		//{
		//	char c = str[i];
		//	RenderableChar renderableChar = getCharInfo(c);
		//	float charWidth = renderableChar.charSize.x * scale;
		//	float charHeight = renderableChar.charSize.y * scale;
		//	float adjustedY = y - (renderableChar.charSize.y - renderableChar.bearingY) * scale;
		//	minY = glm::min(charHeight, minY);
		//	maxY = glm::max(adjustedY, maxY);

		//	char nextC = i < str.length() - 1 ? str[i + 1] : '\0';
		//	//x += font.getKerning(c, nextC) * scale * font.fontSize;
		//	x += renderableChar.advance.x * scale;
		//}

		//return glm::vec2(x, maxY - minY);
		return glm::vec2();
	}

	std::string Font::getStringThatFitsIn(const std::string& originalString, float scale, float maxSizeX, bool leftToRight) const
	{
		float width = 0;

		int i = (leftToRight ? 0 : originalString.length() - 1);
		while (true)
		{
			char c = originalString[i];
			RenderableChar renderableChar = getCharInfo(c);
			float charWidth = renderableChar.charSize.x * scale;

			char prevC = i > 0 ? originalString[i - 1] : '\0';
			char nextC = i < originalString.length() - 1 ? originalString[i + 1] : '\0';
			width += (leftToRight ? (getKerning(c, nextC) * scale) : (getKerning(prevC, c) * scale));
			width += renderableChar.advance.x * scale;

			if (width >= maxSizeX)
			{
				g_logger_assert(i != originalString.length() - 1, "Invalid string width. This input box can't fit this string.");
				if (leftToRight)
				{
					return originalString.substr(0, i);
				}
				else
				{
					return originalString.substr(i + 1, originalString.length() - (i + 1));
				}
			}

			if (leftToRight)
			{
				i++;
				if (i >= originalString.length())
				{
					break;
				}
			}
			else
			{
				i--;
				if (i < 0)
				{
					break;
				}
			}
		}

		return originalString;
	}

	namespace Fonts
	{
		static bool initialized = false;
		static const int hzPadding = 0;
		static const int vtPadding = 0;
		static FT_Library library;
		static robin_hood::unordered_map<std::string, Font> loadedFonts;

		static std::string getFormattedFilepath(const char* filepath, FontSize fontSize);
		static void generateDefaultCharset(Font& font, CharRange defaultCharset);

		void init()
		{
			int error = FT_Init_FreeType(&library);
			if (error)
			{
				g_logger_error("An error occurred during freetype initialization. Font rendering will not work.");
			}

			g_logger_info("Initialized freetype library.");
			initialized = true;
		}

		Font* loadFont(const char* filepath, FontSize fontSize, CharRange defaultCharset)
		{
			g_logger_assert(initialized, "Font library must be initialized to load a font.");

			// If the font is loaded already, return that font
			Font* possibleFont = getFont(filepath, fontSize);
			if (possibleFont != nullptr)
			{
				return possibleFont;
			}

			// Load the new font into freetype.
			FT_Face face;
			int error = FT_New_Face(library, filepath, 0, &face);
			if (error == FT_Err_Unknown_File_Format)
			{
				g_logger_error("Unsupported font file format for '%s'. Could not load font.", filepath);
				return nullptr;
			}
			else if (error)
			{
				g_logger_error("Font could not be opened or read or is broken '%s'. Could not load font.", filepath);
				return nullptr;
			}

			error = FT_Set_Char_Size(face, 0, fontSize * 64, 300, 300);
			if (error)
			{
				g_logger_error("Could not set font size appropriately for '%s'. Is this font non-scalable?", filepath);
				return nullptr;
			}

			// Generate a texture for the font and initialize the font structure
			Font font;
			font.fontFace = face;
			font.fontSize = fontSize;

			std::string formattedFilepath = getFormattedFilepath(filepath, fontSize);
			// The default is 1K texture size if the color is RGBA
			// Since we're using Red component only, we can store 1K x 4 for the same cost
			font.texture = TextureBuilder()
				.setFormat(ByteFormat::R8_UI)
				.setWidth(1024 * 4)
				.setHeight(1024 * 4)
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setTextureType(TextureType::_2D)
				.setFilepath(formattedFilepath.c_str())
				.generateTextureObject()
				.bindTextureObject()
				.generate();

			// Shift right 6, because this is measured in 64ths of a pixel, so we divide by 64 here
			font.lineHeight = face->size->metrics.height >> 6;
			font.ascender = face->ascender >> 6;
			font.descender = face->descender >> 6;

			// TODO: Turn the preset characters into a parameter
			generateDefaultCharset(font, defaultCharset);

			loadedFonts[formattedFilepath] = font;

			return &loadedFonts[formattedFilepath];
		}

		void unloadFont(Font* font)
		{
			if (!font)
			{
				return;
			}

			std::string formattedPath = getFormattedFilepath(font->texture.path, font->fontSize);
			if (font->texture.graphicsId != UINT32_MAX)
			{
				font->texture.destroy();
			}

			loadedFonts.erase(formattedPath);
		}

		void unloadFont(const char* filepath, FontSize fontSize)
		{
			Font* font = getFont(filepath, fontSize);
			if (font)
			{
				unloadFont(font);
			}
		}

		void unloadAllFonts()
		{
			for (auto& pair : loadedFonts)
			{
				pair.second.texture.destroy();
			}
			loadedFonts.clear();
		}

		Font* getFont(const char* filepath, FontSize fontSize)
		{
			std::string formattedFilepath = getFormattedFilepath(filepath, fontSize);
			auto iter = loadedFonts.find(formattedFilepath);
			if (iter != loadedFonts.end())
			{
				return &iter->second;
			}

			return nullptr;
		}

		static std::string getFormattedFilepath(const char* filepath, FontSize fontSize)
		{
			return std::string(filepath) + std::to_string(fontSize);
		}

		static void generateDefaultCharset(Font& font, CharRange defaultCharset)
		{
			uint8* fontBuffer = (uint8*)g_memory_allocate(sizeof(uint8) * font.texture.width * font.texture.height);
			g_memory_zeroMem(fontBuffer, sizeof(uint8) * font.texture.width * font.texture.height);
			uint32 currentLineHeight = 0;
			uint32 currentX = 0;
			uint32 currentY = 0;

			for (uint32 i = defaultCharset.firstCharCode; i <= defaultCharset.lastCharCode; i++)
			{
				FT_UInt glyphIndex = FT_Get_Char_Index(font.fontFace, i);
				if (glyphIndex == 0)
				{
					g_logger_warning("Character code '%d' not found. Missing glyph.", i);
					continue;
				}

				// Load the glyph
				int error = FT_Load_Glyph(font.fontFace, glyphIndex, FT_LOAD_DEFAULT);
				if (error)
				{
					g_logger_error("Freetype could not load glyph for character code '%d'.", i);
					continue;
				}

				// Render the glyph to the bitmap
				// FT_RENDER_MODE_NORMAL renders a 256 (8-bit) grayscale for each pixel
				error = FT_Render_Glyph(font.fontFace->glyph, FT_RENDER_MODE_NORMAL);
				if (error)
				{
					g_logger_error("Freetype could not render glyph for character code '%d'.", i);
					continue;
				}

				currentLineHeight = glm::max(currentLineHeight, font.fontFace->glyph->bitmap.rows);
				if (currentX + font.fontFace->glyph->bitmap.width > (uint32)font.texture.width)
				{
					currentX = 0;
					currentY += currentLineHeight + vtPadding;
					currentLineHeight = font.fontFace->glyph->bitmap.rows;
				}

				if (currentY + currentLineHeight > (uint32)font.texture.height)
				{
					g_logger_error("Cannot continue adding to font. Stopped at char '%d' '%c' because we overran the texture height.", i, i);
					break;
				}

				// Add the glyph to our texture map
				for (uint32 y = 0; y < font.fontFace->glyph->bitmap.rows; y++)
				{
					for (uint32 x = 0; x < font.fontFace->glyph->bitmap.width; x++)
					{
						// Copy the glyph data to our bitmap
						uint32 bufferX = x + currentX;
						uint32 bufferY = font.texture.height - (currentY + 1 + y);
						g_logger_assert(bufferX < (uint32)font.texture.width&& bufferY < (uint32)font.texture.height, "Invalid bufferX, bufferY. Out of bounds greater than tex size.");
						fontBuffer[bufferX + (bufferY * font.texture.width)] =
							font.fontFace->glyph->bitmap.buffer[x + (y * font.fontFace->glyph->bitmap.width)];
					}
				}

				// Shift right 6 to divide by 64, since fonts are measured in 64ths of a pixel
				int flippedY = font.texture.height - (currentY + 1 + font.fontFace->glyph->bitmap.rows);
				font.characterMap[i] = {
					{ (float)(font.fontFace->glyph->metrics.width >> 6), (float)(font.fontFace->glyph->metrics.height >> 6) },
					{ ((float)currentX + 0.5f) / (float)font.texture.width, ((float)flippedY + 0.5f) / (float)font.texture.height },
					{ ((float)font.fontFace->glyph->bitmap.width - 1.0f) / (float)font.texture.width, ((float)font.fontFace->glyph->bitmap.rows - 0.5f) / (float)font.texture.height },
					{ (float)(font.fontFace->glyph->metrics.horiAdvance >> 6), (float)(font.fontFace->glyph->metrics.vertAdvance >> 6) },
					{ (float)(font.fontFace->glyph->metrics.horiBearingY >> 6) }
				};

				currentX += font.fontFace->glyph->bitmap.width + hzPadding;
			}

			// Flip the texture vertically since OpenGL doesn't like it this direction
			uint8* flippedBuffer = (uint8*)g_memory_allocate(sizeof(uint8) * font.texture.width * font.texture.height);
			for (int y = 0; y < font.texture.height; y++)
			{
				uint8* srcScanline = fontBuffer + font.texture.width * y;
				uint8* dstScanline = flippedBuffer + font.texture.width * (font.texture.height - y - 1);
				memcpy(dstScanline, srcScanline, sizeof(uint8) * font.texture.width);
			}

			// Flip texture coords for all chars
			for (robin_hood::pair<char, RenderableChar>& tuple : font.characterMap)
			{
				tuple.second.texCoordStart.y = 1.0f - tuple.second.texCoordStart.y - tuple.second.texCoordSize.y;
			}

			font.texture.uploadSubImage(0, 0, font.texture.width, font.texture.height, flippedBuffer);
			g_memory_free(fontBuffer);
			g_memory_free(flippedBuffer);
		}
	}

	FontSize operator""_px(unsigned long long int numPixels)
	{
		return (uint32)numPixels;
	}

	FontSize operator""_em(long double emSize)
	{
		return (uint32)emSize;
	}
}