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

		static std::unordered_map<std::string, int> nameToIdMap;
		static std::vector<BlockFormat> blockFormats;
		static std::unordered_map<std::string, TextureFormat> textureFormatMap;

		static Texture texCoordsTexture;

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

			for (auto texture : textureFormat["Blocks"])
			{
				const YAML::Node& uvs = texture["UVS"];
				glm::vec2 uv0 = YamlExtended::readVec2("0", uvs);
				glm::vec2 uv1 = YamlExtended::readVec2("1", uvs);
				glm::vec2 uv2 = YamlExtended::readVec2("2", uvs);
				glm::vec2 uv3 = YamlExtended::readVec2("3", uvs);
				const uint16 texCoordId = texture["ID"].as<uint16>();
				TextureFormat format = {
					{ uv0, uv1, uv2, uv3 },
					texCoordId
				};
				textureFormatMap[texture.first.as<std::string>()] = format;
			}

			for (auto block : blockFormat)
			{
				int id = block.second["id"].as<int>();
				std::string side = block.second["side"].as<std::string>();
				std::string top = block.second["top"].as<std::string>();
				std::string bottom = block.second["bottom"].as<std::string>();

				g_logger_info("Loading Block Id: %d", id);
				g_logger_info("Side: %s", side.c_str());
				g_logger_info("Top: %s", top.c_str());
				g_logger_info("Bottom: %s", bottom.c_str());

				nameToIdMap[block.first.as<std::string>()] = id;
				blockFormats.emplace_back(BlockFormat{
					side, top, bottom
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

			uint16 width = (uint16)sqrt(numTextures * 8) + 1;
			uint16 height = width;
			g_logger_info("Width and height of texture coordinates texture.");
			g_logger_info("Width: %d", width);
			g_logger_info("Height: %d", height);
			g_logger_info("Num Textures: %d", numTextures);
			texCoordsTexture = TextureBuilder()
				.setFormat(ByteFormat::R32_F)
				.setMagFilter(FilterMode::None)
				.setMinFilter(FilterMode::None)
				.setWrapS(WrapMode::None)
				.setWrapT(WrapMode::None)
				.setWidth(width)
				.setHeight(height)
				.generate();

			g_memory_free(texCoordsMap);
		}

		const Texture& getTextureCoordinatesTexture()
		{
			return texCoordsTexture;
		}
	}
}
