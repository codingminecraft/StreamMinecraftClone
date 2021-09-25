#include "gameplay/CommandLine.h"
#include "renderer/Font.h"
#include "renderer/Styles.h"
#include "renderer/Renderer.h"

namespace Minecraft
{
	namespace CommandLine
	{
		void update(float dt)
		{
			Font* font = Fonts::getFont("assets/fonts/Minecraft.ttf", 16_px);
			if (font)
			{
				Style transparentSquare = Styles::defaultStyle;
				transparentSquare.color = "#00000055"_hex;
				Renderer::drawFilledSquare2D(glm::vec2(-2.95f, -1.35), glm::vec2(5.1f, 0.1f), transparentSquare, -1);
			}
		}
	}
}
