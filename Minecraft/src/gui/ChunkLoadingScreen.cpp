#include "gui/ChunkLoadingScreen.h"
#include "gui/Gui.h"
#include "gui/GuiElements.h"
#include "core/Scene.h"
#include "core/AppData.h"
#include "world/BlockMap.h"
#include "world/World.h"
#include "world/ChunkManager.h"
#include "renderer/Styles.h"
#include "renderer/Renderer.h"
#include "renderer/Font.h"

namespace Minecraft
{
	namespace ChunkLoadingScreen
	{
		static Sprite dirtTextureSprite;
		static Font* defaultFont;

		const glm::vec2 loadingBarSize = glm::vec2(2.0f, 0.2f);
		const glm::vec2 loadingBarPos = -loadingBarSize / 2.0f;
		const glm::vec2 padding = glm::vec2(0.03f);

		void init()
		{
			dirtTextureSprite.texture = TextureBuilder()
				.setFilepath("assets/images/block/dirt.png")
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setTextureType(TextureType::_2D)
				.setWrapS(WrapMode::Repeat)
				.setWrapT(WrapMode::Repeat)
				.generateTextureObject()
				.bindTextureObject()
				.generate(true);
			dirtTextureSprite.uvStart = glm::vec2(0.0f, 0.0f);
			dirtTextureSprite.uvSize = glm::vec2(12.0f, 6.0f);

			g_logger_info("Loading screen initialized.");
			defaultFont = Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);
		}

		void update(float dt, float percentLoaded)
		{
			static Style dirtStyle = Styles::defaultStyle;
			dirtStyle.color = "#232323ff"_hex;
			dirtTextureSprite.uvSize = glm::vec2(12.0f, 4.0f);
			Renderer::drawTexture2D(dirtTextureSprite, glm::vec2(-3.0f, -1.0f), glm::vec2(6.0f, 2.0f), dirtStyle, -3);
			dirtStyle.color = "#777777ff"_hex;
			dirtTextureSprite.uvSize = glm::vec2(12.0f, 6.0f);
			Renderer::drawTexture2D(dirtTextureSprite, glm::vec2(-3.0f, -1.5f), glm::vec2(6.0f, 3.0f), dirtStyle, -4);

			static Style backgroundStyle = Styles::defaultStyle;
			backgroundStyle.color = "#020202ff"_hex;
			Renderer::drawFilledSquare2D(loadingBarPos, loadingBarSize, backgroundStyle, -1);
			const glm::vec2 innerBarSize = glm::vec2(
				(loadingBarSize.x * percentLoaded) - padding.x * 2.0f,
				loadingBarSize.y - padding.y * 2.0f
			);
			if (innerBarSize.x >= 0)
			{
				Renderer::drawFilledSquare2D(loadingBarPos + padding, innerBarSize, Styles::defaultStyle);
			}

			const std::string loadingWorldText = "Loading Chunks";
			static int numDots = 0;
			static int ticks = 0;
			ticks = (ticks + 1) % 15;
			if (ticks == 14)
			{
				numDots = (numDots + 1) % 3;
			}
			std::string loadingWorldTextWithDots = loadingWorldText;
			for (int i = 0; i < numDots + 1; i++)
			{
				loadingWorldTextWithDots += ".";
			}
			const float loadingWorldFontSize = 0.003f;
			glm::vec2 loadingWorldTextSize = defaultFont->getSize(loadingWorldText, loadingWorldFontSize);
			glm::vec2 loadingWorldPos = glm::vec2(
				-loadingWorldTextSize.x / 2.0f,
				loadingBarPos.y + loadingBarSize.y + padding.y * 2.0f
			);
			Renderer::drawString(loadingWorldTextWithDots, *defaultFont, loadingWorldPos, loadingWorldFontSize, Styles::defaultStyle);

			int percentDoneInt = (int)(percentLoaded * 100.0f);
			std::string text = std::to_string(percentDoneInt) + "%";
			const float fontSize = 0.002f;
			glm::vec2 strSize = defaultFont->getSize(text, fontSize);
			glm::vec2 strPos = glm::vec2(
				loadingBarPos.x + loadingBarSize.x + padding.x,
				loadingBarPos.y + (loadingBarSize.y / 2.0f) - (strSize.y / 2.0f)
			);
			Renderer::drawString(text, *defaultFont, strPos, fontSize, Styles::defaultStyle);
		}

		void free()
		{
			dirtTextureSprite.texture.destroy();
		}
	}
}