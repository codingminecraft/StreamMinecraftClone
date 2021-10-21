#include "core.h"
#include "gui/MainHud.h"
#include "renderer/Sprites.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"

namespace Minecraft
{
	namespace MainHud
	{
		// Internal variables
		static const Sprite* blockCursorSprite = nullptr;
		static const glm::vec2 blockCursorSize = glm::vec2(0.1f, 0.1f);

		void init()
		{
			const std::unordered_map<std::string, Sprite>& menuSprites = Sprites::getSpritesheet("assets/images/hudSpritesheet.yaml");
			blockCursorSprite = &menuSprites.at(std::string("blockCursor"));
		}

		void update(float dt)
		{
			if (blockCursorSprite)
			{
				Renderer::drawTexture2D(*blockCursorSprite, -blockCursorSize * 0.5f, blockCursorSize, Styles::defaultStyle);
			}
		}

		void free()
		{
			blockCursorSprite = nullptr;
		}
	}
}