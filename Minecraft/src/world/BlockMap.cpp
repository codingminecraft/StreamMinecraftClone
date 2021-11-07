#include "world/BlockMap.h"
#include "utils/YamlExtended.h"
#include "renderer/Texture.h"
#include "renderer/Sprites.h"
#include "renderer/Framebuffer.h"
#include "renderer/Shader.h"
#include "renderer/Renderer.h"
#include "core/Application.h"
#include "core/Window.h"

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

		static robin_hood::unordered_map<std::string, int> nameToIdMap;
		static std::vector<BlockFormat> blockFormats;
		// TODO: Ensure that these maps never change throughout a gameplay cycle
		// unless a resource pack is loaded
		static robin_hood::unordered_map<std::string, TextureFormat> textureFormatMap;
		static robin_hood::unordered_map<std::string, TextureFormat> itemTextureFormatMap;
		static robin_hood::unordered_map<std::string, TextureFormat> blockItemTextureMap;

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

			const auto& iter3 = blockItemTextureMap.find(textureName);
			if (iter2 != blockItemTextureMap.end())
			{
				return iter3->second;
			}

			g_logger_error("Unable to find texture '%s'", textureName.c_str());
			return TextureFormat{};
		}

		const BlockFormat& getBlock(const std::string& name)
		{
			int blockId = getBlockId(name);
			return blockFormats[blockId];
		}

		const int getBlockId(const std::string& name)
		{
			const auto& iter = nameToIdMap.find(name);
			if (iter == nameToIdMap.end())
			{
				return 0;
			}
			return iter->second;
		}

		const BlockFormat& getBlock(int blockId)
		{
			if (blockId <= 0 || blockId > blockFormats.size())
			{
				return blockFormats[0];
			}
			return blockFormats[blockId];
		}

		void loadBlocks(const char* textureFormatConfig, const char* itemFormatConfig, const char* blockFormatConfig)
		{
			YAML::Node textureFormat = YamlExtended::readFile(textureFormatConfig);
			YAML::Node blockFormat = YamlExtended::readFile(blockFormatConfig);
			YAML::Node itemFormat = YamlExtended::readFile(itemFormatConfig);

			blockFormats.push_back({
				nullptr,
				nullptr,
				nullptr,
				"",
				true,
				false,
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
				const YAML::Node& textureFormatItems = itemFormat["Items"];
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
				bool isBlendable = block.second["isBlendable"].IsDefined() ? block.second["isBlendable"].as<bool>() : false;
				bool isLightSource = block.second["isLightSource"].IsDefined() ? block.second["isLightSource"].as<bool>() : false;
				int lightLevel = block.second["lightLevel"].IsDefined() ? block.second["lightLevel"].as<int>() : 0;

				g_logger_info("Loading Block Id: %d", id);
				g_logger_info("Side: %s", side.c_str());
				g_logger_info("Top: %s", top.c_str());
				g_logger_info("Bottom: %s", bottom.c_str());

				nameToIdMap[block.first.as<std::string>()] = id;

				const auto& sideTextureIter = textureFormatMap.find(side);
				TextureFormat* sideTexture = nullptr;
				if (sideTextureIter != textureFormatMap.end())
				{
					sideTexture = &sideTextureIter->second;
				}
				const auto& topTextureIter = textureFormatMap.find(top);
				TextureFormat* topTexture = nullptr;
				if (topTextureIter != textureFormatMap.end())
				{
					topTexture = &topTextureIter->second;
				}
				const auto& bottomTextureIter = textureFormatMap.find(bottom);
				TextureFormat* bottomTexture = nullptr;
				if (bottomTextureIter != textureFormatMap.end())
				{
					bottomTexture = &bottomTextureIter->second;
				}

				blockFormats.emplace_back(BlockFormat{
					sideTexture, topTexture, bottomTexture, itemPictureName, isTransparent, isSolid, colorTopByBiome, colorSideByBiome, colorBottomByBiome,
					isBlendable, isLightSource, lightLevel
					});
			}
		}

		void loadBlockItemTextures(const char* blockFormatConfig)
		{
			YAML::Node itemFormat = YamlExtended::readFile(blockFormatConfig);

			if (itemFormat["BlockItems"])
			{
				const YAML::Node& textureFormatItems = itemFormat["BlockItems"];
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
						blockItemTextureMap[texture.first.as<std::string>() + "_as_item"] = format;

						int blockId = nameToIdMap[texture.first.as<std::string>()];
						if (blockId >= 0 && blockId < blockFormats.size())
						{
							BlockFormat& block = blockFormats.at(blockId);
							block.itemPictureName = texture.first.as<std::string>() + "_as_item";
						}
					}
				}
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

		void patchBlockItemTextureMaps(const Texture* blockItemTexture)
		{
			for (auto& it : blockItemTextureMap)
			{
				it.second.texture = blockItemTexture;
			}
		}

		uint32 getTextureCoordinatesTextureId()
		{
			return texCoordsTextureId;
		}

		void generateBlockItemPictures(const char* blockFormatConfig, const char* outputPath)
		{
			YAML::Node blockFormat = YamlExtended::readFile(blockFormatConfig);

			glm::vec3 cameraPos = glm::vec3(-1.0f, 1.0f, 1.0f);
			glm::vec3 cameraOrientation = glm::vec3(-35.0f, -45.0f, 0.0f);
			glm::vec3 direction;
			direction.x = cos(glm::radians(cameraOrientation.y)) * cos(glm::radians(cameraOrientation.x));
			direction.y = sin(glm::radians(cameraOrientation.x));
			direction.z = sin(glm::radians(cameraOrientation.y)) * cos(glm::radians(cameraOrientation.x));
			glm::vec3 cameraForward = glm::normalize(direction);
			glm::vec3 cameraRight = glm::cross(cameraForward, glm::vec3(0, 1, 0));
			glm::vec3 cameraUp = glm::cross(cameraRight, cameraForward);
			glm::mat4 viewMatrix = glm::lookAt(
				cameraPos,
				cameraPos + cameraForward,
				cameraUp
			);
			glm::mat4 projectionMatrix = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 2000.0f);

			Texture texture;
			texture.width = 32;
			texture.height = 32;
			texture.magFilter = FilterMode::Nearest;
			texture.minFilter = FilterMode::Nearest;
			texture.type = TextureType::_2D;
			texture.format = ByteFormat::RGBA8_UI;
			texture.wrapS = WrapMode::None;
			texture.wrapT = WrapMode::None;
			Framebuffer framebuffer = FramebufferBuilder(64, 64)
				.addColorAttachment(texture)
				.generate();
			framebuffer.bind();
			glViewport(0, 0, framebuffer.width, framebuffer.height);

			for (auto block : blockFormat)
			{
				std::string fileOutputPath = outputPath + block.first.as<std::string>() + ".png";
				std::string side = block.second["side"].as<std::string>();
				std::string top = block.second["top"].as<std::string>();
				std::string bottom = block.second["bottom"].as<std::string>();
				std::string itemPictureName = block.second["itemPicture"].IsDefined() ? block.second["itemPicture"].as<std::string>() : "";
				if (itemPictureName == "" && side != "none")
				{
					framebuffer.bind();
					const TextureFormat& sideSprite = getTextureFormat(side);
					const TextureFormat& topSprite = getTextureFormat(top);
					const TextureFormat& bottomSprite = getTextureFormat(bottom);
					Renderer::clearColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
					Renderer::drawTexturedCube(glm::vec3(), glm::vec3(1.0f, 1.0f, 1.0f), sideSprite, topSprite, bottomSprite);
					
					Renderer::flushBatches3D(projectionMatrix, viewMatrix);

					uint8* pixels = framebuffer.readAllPixelsRgb8(0);
					if (!stbi_write_png(fileOutputPath.c_str(), framebuffer.width, framebuffer.height, 4, pixels, sizeof(uint8) * 4 * framebuffer.width))
					{
						g_logger_info("Image write failed because %s", stbi_failure_reason());
					}
					framebuffer.freePixelsRgb8(pixels);
				}
			}

			framebuffer.unbind();
			framebuffer.destroy(true);

			glViewport(0, 0, Application::getWindow().width, Application::getWindow().height);
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

	bool Block::isTransparent() const
	{
		return BlockMap::getBlock(id).isTransparent;
	}
}
