#include "utils/TexturePacker.h"
#include "utils/YamlExtended.h"

#include <stb/stb_image.h>
#include <stb/stb_write.h>
#include <filesystem>
#include <vector>

namespace Minecraft
{
	using namespace CppUtils;

	struct Location
	{
		int x, y, width, height;
		std::string name;
	};

	namespace TexturePacker
	{
		void packTextures(const char* filepath, const char* configFilepath)
		{
			std::vector<Location> textureLocations;

			int numFiles = 0;
			for (auto image : std::filesystem::directory_iterator(filepath))
			{
				if (strcmp(image.path().extension().string().c_str(), ".png") == 0)
				{
					numFiles++;
				}
			}

			int pngOutputWidth = sqrt(numFiles * 32 * 32);
			Logger::Info("Max Width: %d", pngOutputWidth);
			int currentX = 0;
			int currentY = 0;
			int lineHeight = 0;
			List<Pixel> pixels = List<Pixel>(pngOutputWidth);
			for (auto image : std::filesystem::directory_iterator(filepath))
			{
				if (strcmp(image.path().extension().string().c_str(), ".png") != 0) continue;

				int width, height;
				int channels;
				unsigned char* rawPixels = stbi_load(image.path().string().c_str(), &width, &height, &channels, 4);

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
					currentX, currentY,
					width, height,
					image.path().stem().string()
				});

				for (int x = 0; x < width; x++)
				{
					for (int y = 0; y < height; y++)
					{
						int localX = x + currentX;
						int localY = y + currentY;
						Pixel& currentPixel = pixels.get(localY * pngOutputWidth + localX);
						unsigned char* currentRawPixel = &rawPixels[(y * width + x) * 4];
						currentPixel.r = *(currentRawPixel);
						currentPixel.g = *(currentRawPixel + 1);
						currentPixel.b = *(currentRawPixel + 2);
						currentPixel.a = *(currentRawPixel + 3);
					}
				}

				stbi_image_free(rawPixels);

				currentX += width;
			}

			int pngOutputHeight = currentY + lineHeight;
			stbi_write_png("test.png", pngOutputWidth, pngOutputHeight, 4, &pixels.begin()->r, pngOutputWidth * sizeof(Pixel));
		
			YAML::Node textureFormat;
			textureFormat["Blocks"];
			for (const Location& location : textureLocations)
			{
				YAML::Node uvs;
				YamlExtended::writeVec2(
					"0",
					glm::vec2{ (float)(location.x + location.width) / (float)pngOutputWidth, (float)location.y / (float)pngOutputHeight },
					uvs["UVS"]);
				YamlExtended::writeVec2(
					"1",
					glm::vec2{ (float)(location.x + location.width) / (float)pngOutputWidth, (float)(location.y + location.height) / (float)pngOutputHeight },
					uvs["UVS"]);
				YamlExtended::writeVec2(
					"2",
					glm::vec2{ (float)location.x / (float)pngOutputWidth, (float)(location.y + location.height) / (float)pngOutputHeight },
					uvs["UVS"]);
				YamlExtended::writeVec2(
					"3", 
					glm::vec2{ (float)location.x / (float)pngOutputWidth, (float)location.y / (float)pngOutputHeight },
					uvs["UVS"]);


				textureFormat["Blocks"][location.name] = uvs;
			}
			YamlExtended::writeFile(configFilepath, textureFormat);
		}
	}
}