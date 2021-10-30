#include "gameplay/CommandLine.h"
#include "renderer/Font.h"
#include "renderer/Styles.h"
#include "renderer/Renderer.h"
#include "gui/Gui.h"
#include "gui/MainHud.h"
#include "input/Input.h"
#include "world/BlockMap.h"

namespace Minecraft
{
	namespace CommandLine
	{
		extern bool isActive = false;

		void update(float dt, bool parseText)
		{
			Font* font = Fonts::getFont("assets/fonts/Minecraft.ttf", 16_px);
			if (font)
			{
				Style transparentSquare = Styles::defaultStyle;
				transparentSquare.color = "#00000088"_hex;
				glm::vec2 windowPos = glm::vec2(-2.95f, -1.35f);
				glm::vec2 windowSize = glm::vec2(5.9f, 0.1f);

				Gui::beginWindow(windowPos, windowSize);

				static std::array<char, 512> buffer;
				if (!parseText)
				{
					Gui::input("", 0.0015f, buffer.data(), buffer.size(), true);
				}
				else
				{
					if (buffer[0] == '/')
					{
						std::string blockName = std::string(&buffer[1]);
						int blockId = BlockMap::getBlockId(blockName);
						if (blockId)
						{
							MainHud::hotbarBlockIds[0] = blockId;
						}
					}
					else
					{
						// TODO: Display the message to chat
					}

					buffer[0] = '\0';
				}

				Gui::endWindow();
			}
		}
	}
}
