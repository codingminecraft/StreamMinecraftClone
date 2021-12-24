#include "utils/TexturePacker.h"
#include "utils/YamlExtended.h"
#include "core/File.h"

namespace Minecraft
{
	struct Location
	{
		float x, y, width, height;
		std::string name;
	};

	namespace TexturePacker
	{
		void packTextures(const char* filepath, const char* configFilepath, const char* outputFilepath, const char* yamlKeyName, bool generateMips, int texWidth, int texHeight)
		{
			// Return early if the texture packer has already packed the textures and we haven't edited the folder with all the images
			// since then
			if (std::filesystem::exists(configFilepath) && std::filesystem::exists(outputFilepath))
			{
				FileTime directoryWithImagesMetrics = File::getFileTimes(filepath);
				FileTime outputTextureMetrics = File::getFileTimes(outputFilepath);

				// Only return early if the texture was written *after* the last time we
				// edited the images
				if (outputTextureMetrics.lastWrite > directoryWithImagesMetrics.lastWrite)
				{
					return;
				}
				else
				{
					g_logger_info("Texture path '%s' was edited since the last time we cached the textures. Repacking textures.", filepath);
				}
			}

			std::vector<Location> textureLocations;

			int numFiles = 0;
			for (auto& image : std::filesystem::directory_iterator(filepath))
			{
				if (strcmp(image.path().extension().string().c_str(), ".png") == 0)
				{
					numFiles++;
				}
			}

			int pngOutputWidth = (int)sqrt(numFiles * texWidth * texHeight);
			int currentX = 0;
			int currentY = 0;
			int lineHeight = 0;
			int numTexturesUsed = 0;
			// Generate the first output image
			std::vector<Pixel> pixels = std::vector<Pixel>(pngOutputWidth);
			for (auto image : std::filesystem::directory_iterator(filepath))
			{
				if (strcmp(image.path().extension().string().c_str(), ".png") != 0) continue;

				int width, height;
				int channels;
				unsigned char* rawPixels = stbi_load(image.path().string().c_str(), &width, &height, &channels, 4);

				// Shrink the image height if it's bigger than the normal texture height
				height = glm::min(height, texHeight);
				int newX = currentX + width;
				if (newX >= pngOutputWidth)
				{
					currentY += lineHeight;
					currentX = 0;
					lineHeight = 0;
				}

				if (height > lineHeight)
				{
					lineHeight = height;
					pixels.resize((currentY + lineHeight + 1) * pngOutputWidth);
				}

				// Save the current x and current y and width and height and filename
				textureLocations.push_back({
					(float)(currentX), (float)(currentY),
					(float)(width), (float)(height),
					image.path().stem().string()
					});

				for (int x = 0; x < width; x++)
				{
					for (int y = 0; y < height; y++)
					{
						int localX = x + currentX;
						int localY = y + currentY;
						Pixel& currentPixel = pixels.at(localY * pngOutputWidth + localX);
						unsigned char* currentRawPixel = &rawPixels[(y * width + x) * 4];
						currentPixel.r = *(currentRawPixel);
						currentPixel.g = *(currentRawPixel + 1);
						currentPixel.b = *(currentRawPixel + 2);
						currentPixel.a = *(currentRawPixel + 3);
					}
				}

				stbi_image_free(rawPixels);
				numTexturesUsed++;

				currentX += width;
			}

			int pngOutputHeight = currentY + lineHeight;
			stbi_write_png(outputFilepath, pngOutputWidth, pngOutputHeight, 4, pixels.data(), pngOutputWidth * sizeof(Pixel));

			if (generateMips)
			{
				// Figure out how many mip levels we need and generate mipped versions of the files
				int numMipLevels = (uint32)glm::floor(glm::log2(glm::min(pngOutputWidth / texWidth, pngOutputHeight / texHeight))) + 1;
				// Allocate memory for each mip level
				uint8** mipImages = (uint8**)(g_memory_allocate(sizeof(uint8*) * numMipLevels));
				int* widths = (int*)(g_memory_allocate(sizeof(int) * numMipLevels));
				int* heights = (int*)(g_memory_allocate(sizeof(int) * numMipLevels));
				int* texWidths = (int*)(g_memory_allocate(sizeof(int) * numMipLevels));
				int* texHeights = (int*)(g_memory_allocate(sizeof(int) * numMipLevels));
				for (int i = 0; i < numMipLevels; i++)
				{
					int newWidth = glm::max(pngOutputWidth >> (i + 1), 1);
					int newHeight = glm::max(pngOutputHeight >> (i + 1), 1);
					widths[i] = newWidth;
					heights[i] = newHeight;
					texWidths[i] = glm::max(texWidth >> (i + 1), 0);
					texHeights[i] = glm::max(texHeight >> (i + 1), 0);
					mipImages[i] = (uint8*)(g_memory_allocate(sizeof(uint8) * newWidth * newHeight * 4));
				}

				int index = 0;
				int numTexturesPerRow = pngOutputWidth / texWidth;
				for (auto image : std::filesystem::directory_iterator(filepath))
				{
					if (strcmp(image.path().extension().string().c_str(), ".png") != 0) continue;

					int width, height;
					int channels;
					unsigned char* rawPixels = stbi_load(image.path().string().c_str(), &width, &height, &channels, 4);
					height = glm::min(height, texHeight);

					for (int i = 0; i < numMipLevels; i++)
					{
						int imageWidth = widths[i];
						int imageHeight = heights[i];
						int xPos = (index % numTexturesPerRow) * texWidths[i];
						int yPos = (index / numTexturesPerRow) * texHeights[i];
						uint8* outputLocation = &mipImages[i][((yPos * imageWidth) + xPos) * 4];
						stbir_resize_uint8(rawPixels, width, height, width * 4 * sizeof(uint8),
							outputLocation, texWidths[i], texHeights[i], widths[i] * 4 * sizeof(uint8), 4);
					}
					stbi_image_free(rawPixels);
					index++;
				}

				// Write all the mip images to disk
				for (int i = 0; i < numMipLevels; i++)
				{
					std::string outputFilename = std::string(outputFilepath) + ".mip." + std::to_string(i + 1) + ".png";
					stbi_write_png(outputFilename.c_str(), widths[i], heights[i], 4, mipImages[i], widths[i] * 4 * sizeof(uint8));
					g_memory_free(mipImages[i]);
				}
				g_memory_free(mipImages);
				g_memory_free(widths);
				g_memory_free(heights);
				g_memory_free(texWidths);
				g_memory_free(texHeights);
			}

			YAML::Node textureFormat;
			uint16 uid = 0;
			for (const Location& location : textureLocations)
			{
				YAML::Node uvs;
				YamlExtended::writeVec2(
					"0",
					glm::vec2{ (location.x + location.width) / (float)pngOutputWidth, location.y / (float)pngOutputHeight },
					uvs["UVS"]);
				YamlExtended::writeVec2(
					"1",
					glm::vec2{ (location.x + location.width) / (float)pngOutputWidth, (location.y + location.height) / (float)pngOutputHeight },
					uvs["UVS"]);
				YamlExtended::writeVec2(
					"2",
					glm::vec2{ location.x / (float)pngOutputWidth, (location.y + location.height) / (float)pngOutputHeight },
					uvs["UVS"]);
				YamlExtended::writeVec2(
					"3",
					glm::vec2{ location.x / (float)pngOutputWidth, location.y / (float)pngOutputHeight },
					uvs["UVS"]);

				textureFormat[yamlKeyName][location.name] = uvs;
				textureFormat[yamlKeyName][location.name]["ID"] = uid++;
				g_logger_assert(uid <= 2 << 12, "We ran out of space. Add more bits to the vertex specification for texture IDs");
			}
			YamlExtended::writeFile(configFilepath, textureFormat);
		}
	}
}