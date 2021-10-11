#include "world/BlockMap.h"
#include "utils/YamlExtended.h"
#include "renderer/Texture.h"

namespace Minecraft
{
	namespace BlockMap
	{
		extern Block NULL_BLOCK = {
			0,
			0,
			0,
			0
		};

		extern Block AIR_BLOCK = {
			1,
			0,
			0,
			0
		};

		static std::unordered_map<std::string, int> nameToIdMap;
		static std::vector<BlockFormat> blockFormats;
		static std::unordered_map<std::string, TextureFormat> textureFormatMap;

		static uint32 texCoordsTextureId;
		static uint32 texCoordsBufferId;

		const TextureFormat& getTextureFormat(const std::string& textureName)
		{
			return textureFormatMap[textureName];
		}

		const BlockFormat& getBlock(const std::string& name)
		{
			int blockId = nameToIdMap[name];
			return blockFormats[blockId];
		}

		const BlockFormat& getBlock(int blockId)
		{
			if (blockId <= 0 || blockId > blockFormats.size())
			{
				return blockFormats[0];
			}
			return blockFormats.at(blockId);
		}

		void loadBlocks(const char* textureFormatConfig, const char* blockFormatConfig)
		{
			YAML::Node textureFormat = YamlExtended::readFile(textureFormatConfig);
			YAML::Node blockFormat = YamlExtended::readFile(blockFormatConfig);

			blockFormats.push_back({
				"",
				"",
				"",
				true,
				false
				});

			if (textureFormat["Blocks"])
			{
				const YAML::Node& textureFormatBlocks = textureFormat["Blocks"];
				for (auto texture : textureFormatBlocks)
				{
					if (texture.second["UVS"])
					{
						const YAML::Node& uvs = texture.second["UVS"];
						glm::vec2 uv0 = YamlExtended::readVec2("0", uvs);
						glm::vec2 uv1 = YamlExtended::readVec2("1", uvs);
						glm::vec2 uv2 = YamlExtended::readVec2("2", uvs);
						glm::vec2 uv3 = YamlExtended::readVec2("3", uvs);
						const uint16 texCoordId = texture.second["ID"].as<uint16>();
						TextureFormat format = {
							{ uv0, uv1, uv2, uv3 },
							texCoordId
						};
						textureFormatMap[texture.first.as<std::string>()] = format;
					}
				}
			}

			for (auto block : blockFormat)
			{
				int id = block.second["id"].as<int>();
				std::string side = block.second["side"].as<std::string>();
				std::string top = block.second["top"].as<std::string>();
				std::string bottom = block.second["bottom"].as<std::string>();
				bool isTransparent = block.second["isTransparent"].as<bool>();
				bool isSolid = block.second["isSolid"].as<bool>();

				g_logger_info("Loading Block Id: %d", id);
				g_logger_info("Side: %s", side.c_str());
				g_logger_info("Top: %s", top.c_str());
				g_logger_info("Bottom: %s", bottom.c_str());

				nameToIdMap[block.first.as<std::string>()] = id;
				blockFormats.emplace_back(BlockFormat{
					side, top, bottom, isTransparent, isSolid
					});
			}
		}

		void uploadTextureCoordinateMapToGpu()
		{
			uint16 numTextures = (uint16)textureFormatMap.size();
			float* texCoordsMap = (float*)g_memory_allocate(sizeof(float) * 8 * numTextures);
			for (const auto& pair : textureFormatMap)
			{
				const TextureFormat& texFormat = pair.second;
				uint16 startingLocation = texFormat.id * 8;
				g_logger_assert(startingLocation + 7 < 8 * numTextures, "Invalid texture location.");

				texCoordsMap[startingLocation + 0] = texFormat.uvs[0].x;
				texCoordsMap[startingLocation + 1] = texFormat.uvs[0].y;

				texCoordsMap[startingLocation + 2] = texFormat.uvs[1].x;
				texCoordsMap[startingLocation + 3] = texFormat.uvs[1].y;

				texCoordsMap[startingLocation + 4] = texFormat.uvs[2].x;
				texCoordsMap[startingLocation + 5] = texFormat.uvs[2].y;

				texCoordsMap[startingLocation + 6] = texFormat.uvs[3].x;
				texCoordsMap[startingLocation + 7] = texFormat.uvs[3].y;
			}

			g_logger_info("Num Textures: %d", numTextures);
			glGenBuffers(1, &texCoordsBufferId);
			glBindBuffer(GL_TEXTURE_BUFFER, texCoordsBufferId);
			glBufferData(GL_TEXTURE_BUFFER, sizeof(float) * 8 * numTextures, texCoordsMap, GL_STATIC_DRAW);

			glGenTextures(1, &texCoordsTextureId);
			glBindTexture(GL_TEXTURE_BUFFER, texCoordsTextureId);
			glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, texCoordsBufferId);

			glBindBuffer(GL_TEXTURE_BUFFER, 0);
			glBindTexture(GL_TEXTURE_BUFFER, 0);

			g_memory_free(texCoordsMap);
		}

		uint32 getTextureCoordinatesTextureId()
		{
			return texCoordsTextureId;
		}
	}

	bool operator==(const Block& a, const Block& b)
	{
		return a.id == b.id;
	}
}
