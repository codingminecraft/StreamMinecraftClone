#ifndef MINECRAFT_SPRITES_H
#define MINECRAFT_SPRITES_H
#include "core.h"
#include "renderer/Texture.h"

namespace Minecraft
{
	struct Texture;

	struct Sprite
	{
		Texture texture;
		glm::vec2 uvStart;
		glm::vec2 uvSize;
	};

	namespace Sprites
	{
		const robin_hood::unordered_map<std::string, Sprite>& getSpritesheet(const char* filepath);
		const void freeAllSpritesheets();
	}
}

#endif