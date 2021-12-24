#ifndef MINECRAFT_TERRAIN_GENERATOR_H
#define MINECRAFT_TERRAIN_GENERATOR_H
#include "core.h"

namespace Minecraft
{
	namespace TerrainGenerator
	{
		void init(const char* terrainNoiseConfig, int seed);
		void free();

		bool getIsCave(int x, int y, int z, int16 maxBiomeHeight);
		int16 getHeight(int x, int z, float minBiomeHeight, float maxBiomeHeight);
		float getNormalizedHeight(int x, int z);
		float getNoise(int x, int z, int noiseLevel);
	}
}

#endif 