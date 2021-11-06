#ifndef MINECRAFT_TERRAIN_GENERATOR_H
#define MINECRAFT_TERRAIN_GENERATOR_H
#include "core.h"

namespace Minecraft
{
	namespace TerrainGenerator
	{
		void outputNoiseToTextures();

		int16 getHeight(const SimplexNoise& generator, int x, int z, float minBiomeHeight, float maxBiomeHeight);
		float getNormalizedHeight(const SimplexNoise& generator, int x, int z);
		float getNoise(const SimplexNoise& generator, int x, int z, int noiseLevel);
	}
}

#endif 