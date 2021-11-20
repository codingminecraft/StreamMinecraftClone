#include "renderer/Sprites.h"
#include "renderer/Texture.h"
#include "utils/YamlExtended.h"

namespace Minecraft
{
	struct Spritesheet
	{
		Texture texture;
		robin_hood::unordered_map<std::string, Sprite> sprites;
	};

	namespace Sprites
	{
		// Internal variables
		static robin_hood::unordered_map<std::string, Spritesheet> spritesheets;

		// Internal functions
		static void load(const char* filepath);

		const robin_hood::unordered_map<std::string, Sprite>& getSpritesheet(const char* filepath)
		{
			auto iter = spritesheets.find(std::string(filepath));
			if (iter == spritesheets.end())
			{
				load(filepath);
				iter = spritesheets.find(std::string(filepath));
				g_logger_assert(iter != spritesheets.end(), "Error loading spritesheet %s", filepath);
			}

			return iter->second.sprites;
		}

		const void freeAllSpritesheets()
		{
			for (auto& pair : spritesheets)
			{
				pair.second.texture.destroy();
			}
			spritesheets.clear();
		}

		static void load(const char* filepath)
		{
			YAML::Node spritesheetConfig = YamlExtended::readFile(filepath);

			Spritesheet spritesheet{};

			std::string textureFilepath;
			bool hasFilepath = false;
			if (spritesheetConfig["texture"])
			{
				const YAML::Node& textureYaml = spritesheetConfig["texture"];
				if (textureYaml["filepath"])
				{
					textureFilepath = textureYaml["filepath"].as<std::string>();
					hasFilepath = true;
				}
			}
			if (!hasFilepath)
			{
				g_logger_warning("To load a spritesheet, you must add a texture and filepath '%s'.", filepath);
			}

			spritesheet.texture = TextureBuilder()
				.setFilepath(textureFilepath.c_str())
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setTextureType(TextureType::_2D)
				.setWrapS(WrapMode::None)
				.setWrapT(WrapMode::None)
				.generateTextureObject()
				.bindTextureObject()
				.generate(true);

			for (auto spriteConfig : spritesheetConfig)
			{
				const std::string spriteName = spriteConfig.first.as<std::string>();
				if (spriteConfig.second["uvStart"] && spriteConfig.second["uvSize"])
				{
					Sprite sprite;
					sprite.uvStart = YamlExtended::readVec2("uvStart", spriteConfig.second);
					sprite.uvStart = {
						sprite.uvStart.x / spritesheet.texture.width,
						sprite.uvStart.y / spritesheet.texture.height
					};
					sprite.uvSize = YamlExtended::readVec2("uvSize", spriteConfig.second);
					sprite.uvSize = {
						sprite.uvSize.x / spritesheet.texture.width,
						sprite.uvSize.y / spritesheet.texture.height
					};
					sprite.texture = spritesheet.texture;
					spritesheet.sprites[spriteName] = sprite;
				}
				else if (spriteName == "texture")
				{
					continue;
				}
				else
				{
					g_logger_warning("Unable to load sprite '%s'. Missing 'uvStart' or 'uvSize'", spriteName.c_str());
				}
			}

			spritesheets[std::string(filepath)] = spritesheet;
		}
	}
}