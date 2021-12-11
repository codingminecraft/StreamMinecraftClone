#include "world/TerrainGenerator.h"
#include "core/File.h"
#include "core/AppData.h"
#include "utils/CMath.h"

namespace Minecraft
{
	namespace TerrainGenerator
	{
		const int numNoise = 5;
		const float scale[numNoise] = { 0.002f, 0.005f, 0.04f , 0.015f, 0.004f };
		const float weights[numNoise] = { 0.6f, 0.2f, 0.05f, 0.1f, 0.05f };

		const int numCaveNoise = 4;
		const float caveScale[numNoise] = { 0.005f, 0.015f, 0.02f, 0.00345f };

		void outputNoiseToTextures()
		{
			const int textureWidth = 512;
			const int textureHeight = 512;
			File::createDirIfNotExists("assets/generated/terrainTextures");

			SimplexNoise generator = SimplexNoise();

			const int numTextures = numNoise;
			uint8* textures = (uint8*)g_memory_allocate(sizeof(uint8) * textureWidth * textureHeight * numTextures);
			uint8* textureData[numTextures];
			uint8* finalTexture = (uint8*)g_memory_allocate(sizeof(uint8) * textureWidth * textureHeight);
			for (int i = 0; i < numTextures; i++)
			{
				textureData[i] = textures + textureWidth * textureHeight * i;
			}

			for (int x = 0; x < textureWidth; x++)
			{
				for (int z = 0; z < textureHeight; z++)
				{
					for (int i = 0; i < numTextures; i++)
					{
						float noise = getNoise(generator, x, z, i);
						textureData[i][x + (z * textureWidth)] = (uint8)(noise * 255.0f);
					}

					float normalizedHeight = getNormalizedHeight(generator, x, z);
					finalTexture[x + (z * textureWidth)] = (uint8)(normalizedHeight * 255.0f);
				}
			}

			for (int i = 0; i < numTextures; i++)
			{
				std::string outPath = std::string("assets/generated/terrainTextures/noise") + std::to_string(i) + std::string(".png");
				stbi_write_png(outPath.c_str(), textureWidth, textureHeight, 1, textureData[i], sizeof(uint8) * textureWidth);
			}

			stbi_write_png("assets/generated/terrainTextures/combinedNoise.png", textureWidth, textureHeight, 1, finalTexture, sizeof(uint8) * textureWidth);

			g_memory_free(textures);
			g_memory_free(finalTexture);
		}

		bool getIsCave(const SimplexNoise& generator, int x, int y, int z, int16 maxBiomeHeight)
		{
			// If we're at ground level, only 10% chance of the cave peeking through the ground
			if (y < maxBiomeHeight || (y == maxBiomeHeight && (float)rand() / (float)RAND_MAX) < 0.1f)
			{
				// Cave pocket generator
				float noise1 = CMath::mapRange(
					generator.fractal(
						4,
						(float)(x)*caveScale[0],
						(float)(y)*caveScale[0],
						(float)(z)*caveScale[0]
					),
					-1.0f,
					1.0f,
					0.0f,
					1.0f
				);
				float noise2 = CMath::mapRange(
					generator.fractal(
						4,
						(float)(x)*scale[1],
						(float)(y)*scale[1],
						(float)(z)*scale[1]
					),
					-1.0f,
					1.0f,
					0.0f,
					1.0f
				);
				if (noise1 < 0.3f && noise2 < 0.3f)
				{
					return true;
				}

				// Check if it's a wormy kind of cave
				float noise3 = CMath::mapRange(
					generator.fractal(
						4,
						(float)(x)*scale[2],
						(float)(y)*scale[2],
						(float)(z)*scale[2]
					),
					-1.0f,
					1.0f,
					0.0f,
					1.0f
				);
				float noise4 = CMath::mapRange(
					generator.fractal(
						4,
						(float)(x)*scale[3],
						(float)(y)*scale[3],
						(float)(z)*scale[3]
					),
					-1.0f,
					1.0f,
					0.0f,
					1.0f
				);
				return noise3 < 0.3f && noise4 < 0.3f;
			}
			return false;
		}

		int16 getHeight(const SimplexNoise& generator, int x, int z, float minBiomeHeight, float maxBiomeHeight)
		{
			float normalizedHeight = TerrainGenerator::getNormalizedHeight(generator, x, z);
			return (int16)CMath::mapRange(normalizedHeight, 0.0f, 1.0f, minBiomeHeight, maxBiomeHeight);
		}

		float getNormalizedHeight(const SimplexNoise& generator, int x, int z)
		{
			float noise[numNoise];
			for (int i = 0; i < numNoise; i++)
			{
				noise[i] = getNoise(generator, x, z, i);
			}

			float blendedNoise = 0.0f;
			for (int i = 0; i < numNoise; i++)
			{
				blendedNoise += noise[i] * weights[i];
			}

			return blendedNoise;
		}

		float getNoise(const SimplexNoise& generator, int x, int z, int noiseLevel)
		{
			return CMath::mapRange(
				generator.fractal(
					4,
					(float)(x)*scale[noiseLevel],
					(float)(z)*scale[noiseLevel]
				),
				-1.0f,
				1.0f,
				0.0f,
				1.0f
			);
		}
	}
}