#include "world/TerrainGenerator.h"
#include "core/File.h"
#include "core/AppData.h"
#include "utils/CMath.h"
#include "utils/YamlExtended.h"

namespace Minecraft
{
	struct WeightedNoise
	{
		fnl_state state;
		float weight;
	};

	namespace TerrainGenerator
	{
		// Internal variables
		static std::array<WeightedNoise, 5> terrainNoiseGenerators;

		// Internal functions
		static fnl_noise_type toFnlNoiseType(const std::string& noiseTypeAsString);
		static fnl_fractal_type toFnlFractalType(const std::string& fractalTypeAsString);

		void init(const char* terrainNoiseConfig, int seed)
		{
			for (int i = 0; i < terrainNoiseGenerators.size(); i++)
			{
				terrainNoiseGenerators[i].state = fnlCreateState();
				// Offset the seed for each noise generator, that way the 
				// values aren't correlated
				terrainNoiseGenerators[i].state.seed = seed + (i * 7);
			}

			YAML::Node terrainNoise = YamlExtended::readFile(terrainNoiseConfig);
			int numGenerators = 0;
			for (auto generator : terrainNoise)
			{
				// TODO: Increase error checking here so it doesn't crash on bad input configuration files
				bool isWellDefined = generator.second["general"].IsDefined();
				if (isWellDefined)
				{
					if (generator.second["general"])
					{
						// Parse General section
						g_logger_info("Parsing terrain noise generator '%s'", generator.first.as<std::string>().c_str());
						const YAML::Node& generalSection = generator.second["general"];
						std::string noiseTypeAsString = generalSection["noiseType"].as<std::string>();
						fnl_noise_type noiseType = toFnlNoiseType(noiseTypeAsString);
						float frequency = generalSection["frequency"].as<float>();
						terrainNoiseGenerators[numGenerators].state.noise_type = noiseType;
						terrainNoiseGenerators[numGenerators].state.frequency = frequency;

						if (generator.second["weight"].IsDefined())
						{
							terrainNoiseGenerators[numGenerators].weight = generator.second["weight"].as<float>();
						}
						else
						{
							g_logger_error("No weight was defined for noise generator '%s'. Defaulting to weight of 0.0f", generator.first.as<std::string>().c_str());
							terrainNoiseGenerators[numGenerators].weight = 0.0f;
						}
					}
					else
					{
						g_logger_error("All noise generators must have a general section. Ignoring generator '%s'", generator.first.as<std::string>().c_str());
					}

					if (generator.second["fractal"])
					{
						const YAML::Node& fractalSection = generator.second["fractal"];
						std::string fractalTypeAsString = fractalSection["fractalType"].as<std::string>();
						fnl_fractal_type type = toFnlFractalType(fractalTypeAsString);
						int octaves = fractalSection["octaves"].as<int>();
						float lacunarity = fractalSection["lacunarity"].as<float>();
						float gain = fractalSection["gain"].as<float>();
						float weightedStrength = fractalSection["weightedStrength"].as<float>();
						
						terrainNoiseGenerators[numGenerators].state.fractal_type = type;
						terrainNoiseGenerators[numGenerators].state.octaves = octaves;
						terrainNoiseGenerators[numGenerators].state.lacunarity = lacunarity;
						terrainNoiseGenerators[numGenerators].state.gain = gain;
						terrainNoiseGenerators[numGenerators].state.weighted_strength = weightedStrength;
					}
					else
					{
						terrainNoiseGenerators[numGenerators].state.fractal_type = fnl_fractal_type::FNL_FRACTAL_NONE;
					}

					// Increase generator index
					numGenerators++;
					if (numGenerators >= terrainNoiseGenerators.size())
					{
						g_logger_warning("Only support up to 5 generators right now. Ignoring the rest of the generators in '%s'", terrainNoiseConfig);
						break;
					}
				}
			}

			g_logger_info("Go to https://github.com/Auburn/FastNoiseLite/releases to download an executable to experiment with different types of noises.\n"
				"If you are happy with what you create, you can just input that noise value into the configuration file.");
		}

		void free()
		{
		}

		bool getIsCave(int x, int y, int z, int16 maxBiomeHeight)
		{
			// If we're at ground level, only 10% chance of the cave peeking through the ground
			//if (y < maxBiomeHeight || (y == maxBiomeHeight && (float)rand() / (float)RAND_MAX) < 0.1f)
			//{
			//	// Cave pocket generator
			//	float noise1 = CMath::mapRange(
			//		generator.fractal(
			//			4,
			//			(float)(x)*caveScale[0],
			//			(float)(y)*caveScale[0],
			//			(float)(z)*caveScale[0]
			//		),
			//		-1.0f,
			//		1.0f,
			//		0.0f,
			//		1.0f
			//	);
			//	float noise2 = CMath::mapRange(
			//		generator.fractal(
			//			4,
			//			(float)(x)*scale[1],
			//			(float)(y)*scale[1],
			//			(float)(z)*scale[1]
			//		),
			//		-1.0f,
			//		1.0f,
			//		0.0f,
			//		1.0f
			//	);
			//	if (noise1 < 0.3f && noise2 < 0.3f)
			//	{
			//		return true;
			//	}

			//	// Check if it's a wormy kind of cave
			//	float noise3 = CMath::mapRange(
			//		generator.fractal(
			//			4,
			//			(float)(x)*scale[2],
			//			(float)(y)*scale[2],
			//			(float)(z)*scale[2]
			//		),
			//		-1.0f,
			//		1.0f,
			//		0.0f,
			//		1.0f
			//	);
			//	float noise4 = CMath::mapRange(
			//		generator.fractal(
			//			4,
			//			(float)(x)*scale[3],
			//			(float)(y)*scale[3],
			//			(float)(z)*scale[3]
			//		),
			//		-1.0f,
			//		1.0f,
			//		0.0f,
			//		1.0f
			//	);
			//	return noise3 < 0.3f && noise4 < 0.3f;
			//}
			return false;
		}

		int16 getHeight(int x, int z, float minBiomeHeight, float maxBiomeHeight)
		{
			float normalizedHeight = TerrainGenerator::getNormalizedHeight(x, z);
			return (int16)CMath::mapRange(normalizedHeight, 0.0f, 1.0f, minBiomeHeight, maxBiomeHeight);
		}

		float getNormalizedHeight(int x, int z)
		{
			std::array<float, terrainNoiseGenerators.size()> noise;
			for (int i = 0; i < noise.size(); i++)
			{
				noise[i] = getNoise(x, z, i);
			}

			float blendedNoise = 0.0f;
			float weightSums = 0.0f;
			for (int i = 0; i < noise.size(); i++)
			{
				blendedNoise += noise[i] * terrainNoiseGenerators[i].weight;
				weightSums += terrainNoiseGenerators[i].weight;
			}

			// Divide by the weight of the sums to normalize the value again
			blendedNoise /= weightSums;

			// Raise it to a power to flatten valleys and increase mountains
			blendedNoise = glm::pow(blendedNoise, 1.19f);

			return blendedNoise;
		}

		float getNoise(int x, int z, int noiseLevel)
		{
			return CMath::mapRange(
				fnlGetNoise2D(
					&terrainNoiseGenerators[noiseLevel].state,
					(float)(x),
					(float)(z)
				),
				-1.0f,
				1.0f,
				0.0f,
				1.0f
			);
		}

		static fnl_noise_type toFnlNoiseType(const std::string& noiseTypeAsString)
		{
			if (noiseTypeAsString == "OpenSimplex2")
			{
				return fnl_noise_type::FNL_NOISE_OPENSIMPLEX2;
			}
			else if (noiseTypeAsString == "OpenSimplex2s")
			{
				return fnl_noise_type::FNL_NOISE_OPENSIMPLEX2S;
			}
			else if (noiseTypeAsString == "Cellular")
			{
				return fnl_noise_type::FNL_NOISE_CELLULAR;
			}
			else if (noiseTypeAsString == "Perlin")
			{
				return fnl_noise_type::FNL_NOISE_PERLIN;
			}
			else if (noiseTypeAsString == "ValueCubic")
			{
				return fnl_noise_type::FNL_NOISE_VALUE_CUBIC;
			}
			else if (noiseTypeAsString == "Value")
			{
				return fnl_noise_type::FNL_NOISE_VALUE;
			}

			g_logger_error("Unknown noise type '%s'. Defaulting to OpenSimplex2 noise type.", noiseTypeAsString.c_str());
			return fnl_noise_type::FNL_NOISE_OPENSIMPLEX2;
		}

		static fnl_fractal_type toFnlFractalType(const std::string& fractalTypeAsString)
		{
			if (fractalTypeAsString == "FBM")
			{
				return fnl_fractal_type::FNL_FRACTAL_FBM;
			}
			else if (fractalTypeAsString == "Ridged")
			{
				return fnl_fractal_type::FNL_FRACTAL_RIDGED;
			}
			else if (fractalTypeAsString == "PingPong")
			{
				return fnl_fractal_type::FNL_FRACTAL_PINGPONG;
			}
			else if (fractalTypeAsString == "None")
			{
				return fnl_fractal_type::FNL_FRACTAL_NONE;
			}

			g_logger_error("Unknown fractal type '%s'. Defaulting to fractal type 'None'", fractalTypeAsString.c_str());
			return fnl_fractal_type::FNL_FRACTAL_NONE;
		}
	}
}