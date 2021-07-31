#include "world/BlockMap.h"
#include "utils/YamlExtended.h"

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

		static std::unordered_map<std::string, int> nameToIdMap;
		static std::vector<BlockFormat> blockFormats;
		static std::unordered_map<std::string, TextureFormat> textureFormatMap;

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
				true
			});

			for (auto block : blockFormat)
			{
				int id = block.second["id"].as<int>();
				std::string side = block.second["side"].as<std::string>();
				std::string top = block.second["top"].as<std::string>();
				std::string bottom = block.second["bottom"].as<std::string>();
				//Logger::Info("Id: %d", id);
				//Logger::Info("Side: %s", side.c_str());
				//Logger::Info("Top: %s", top.c_str());
				//Logger::Info("Bottom: %s", bottom.c_str());

				nameToIdMap[block.first.as<std::string>()] = id;
				blockFormats.push_back({
					side, top, bottom
					});

				{
					const YAML::Node& uvs = textureFormat["Blocks"][side]["UVS"];
					glm::vec2 uv0 = YamlExtended::readVec2("0", uvs);
					glm::vec2 uv1 = YamlExtended::readVec2("1", uvs);
					glm::vec2 uv2 = YamlExtended::readVec2("2", uvs);
					glm::vec2 uv3 = YamlExtended::readVec2("3", uvs);
					TextureFormat format = {
						{ uv0, uv1, uv2, uv3 }
					};
					textureFormatMap[side] = format;
				}

				{
					const YAML::Node& uvs = textureFormat["Blocks"][top]["UVS"];
					glm::vec2 uv0 = YamlExtended::readVec2("0", uvs);
					glm::vec2 uv1 = YamlExtended::readVec2("1", uvs);
					glm::vec2 uv2 = YamlExtended::readVec2("2", uvs);
					glm::vec2 uv3 = YamlExtended::readVec2("3", uvs);
					TextureFormat format = {
						{ uv0, uv1, uv2, uv3 }
					};
					textureFormatMap[top] = format;
				}

				{
					const YAML::Node& uvs = textureFormat["Blocks"][bottom]["UVS"];
					glm::vec2 uv0 = YamlExtended::readVec2("0", uvs);
					glm::vec2 uv1 = YamlExtended::readVec2("1", uvs);
					glm::vec2 uv2 = YamlExtended::readVec2("2", uvs);
					glm::vec2 uv3 = YamlExtended::readVec2("3", uvs);
					TextureFormat format = {
						{ uv0, uv1, uv2, uv3 }
					};
					textureFormatMap[bottom] = format;
				}
			}
		}
	}
}