#include "world/BlockMap.h"
#include "utils/YamlExtended.h"
#include "renderer/Texture.h"
#include "renderer/Sprites.h"

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
		static std::unordered_map<std::string, TextureFormat> itemTextureFormatMap;

		static uint32 texCoordsTextureId;
		static uint32 texCoordsBufferId;

		const TextureFormat& getTextureFormat(const std::string& textureName)
		{
			const auto& iter = textureFormatMap.find(textureName);
			if (iter != textureFormatMap.end())
			{
				return iter->second;
			}

			const auto& iter2 = itemTextureFormatMap.find(textureName);
			if (iter2 != itemTextureFormatMap.end())
			{
				return iter2->second;
			}

			g_logger_error("Unable to find texture '%s'", textureName.c_str());
			return TextureFormat{};
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

		void loadBlocks(const char* textureFormatConfig, const char* itemFormatConfig, const char* blockFormatConfig)
		{
			YAML::Node textureFormat = YamlExtended::readFile(textureFormatConfig);
			YAML::Node blockFormat = YamlExtended::readFile(blockFormatConfig);
			YAML::Node itemFormat = YamlExtended::readFile(itemFormatConfig);

			blockFormats.push_back({
				"",
				"",
				"",
				"",
				true,
				false,
				false,
				false,
				false,
				false,
				0
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
							texCoordId,
							nullptr
						};
						textureFormatMap[texture.first.as<std::string>()] = format;
					}
				}
			}

			if (itemFormat["Items"])
			{
				const YAML::Node& textureFormatItems = textureFormat["Items"];
				for (auto texture : textureFormatItems)
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
							texCoordId,
							nullptr
						};
						itemTextureFormatMap[texture.first.as<std::string>()] = format;
					}
				}
			}

			for (auto block : blockFormat)
			{
				int id = block.second["id"].as<int>();
				std::string side = block.second["side"].as<std::string>();
				std::string top = block.second["top"].as<std::string>();
				std::string bottom = block.second["bottom"].as<std::string>();
				std::string itemPictureName = block.second["itemPicture"].IsDefined() ? block.second["itemPicture"].as<std::string>() : "";
				bool isTransparent = block.second["isTransparent"].as<bool>();
				bool isSolid = block.second["isSolid"].as<bool>();
				bool colorTopByBiome = block.second["colorTopByBiome"].IsDefined() ? block.second["colorTopByBiome"].as<bool>() : false;
				bool colorSideByBiome = block.second["colorSideByBiome"].IsDefined() ? block.second["colorSideByBiome"].as<bool>() : false;
				bool colorBottomByBiome = block.second["colorBottomByBiome"].IsDefined() ? block.second["colorBottomByBiome"].as<bool>() : false;
				bool isLightSource = block.second["isLightSource"].IsDefined() ? block.second["isLightSource"].as<bool>() : false;
				int lightLevel = block.second["lightLevel"].IsDefined() ? block.second["lightLevel"].as<int>() : 0;

				g_logger_info("Loading Block Id: %d", id);
				g_logger_info("Side: %s", side.c_str());
				g_logger_info("Top: %s", top.c_str());
				g_logger_info("Bottom: %s", bottom.c_str());

				nameToIdMap[block.first.as<std::string>()] = id;
				blockFormats.emplace_back(BlockFormat{
					side, top, bottom, itemPictureName, isTransparent, isSolid, colorTopByBiome, colorSideByBiome, colorBottomByBiome,
					isLightSource, lightLevel
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

		void patchTextureMaps(const Texture* blockTexture, const Texture* itemTexture)
		{
			for (auto& it : textureFormatMap)
			{
				it.second.texture = blockTexture;
			}

			for (auto& it : itemTextureFormatMap)
			{
				it.second.texture = itemTexture;
			}
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

	bool operator!=(const Block& a, const Block& b)
	{
		return !(a == b);
	}

	bool Block::isLightSource() const
	{
		return BlockMap::getBlock(id).isLightSource;
	}
}
