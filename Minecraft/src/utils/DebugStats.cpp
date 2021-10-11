#include "core.h"
#include "utils/DebugStats.h"
#include "renderer/Font.h"
#include "renderer/Styles.h"
#include "renderer/Renderer.h"
#include "utils/CMath.h"
#include "world/World.h"

namespace Minecraft
{
	namespace DebugStats
	{
		extern uint32 numDrawCalls = 0;
		extern float lastFrameTime = 0.16f;
		extern glm::vec3 playerPos = glm::vec3();
		extern glm::vec3 playerOrientation = glm::vec3();
		extern uint32 minVertCount = 0;
		extern uint32 maxVertCount = 0;
		extern float avgVertCount = 0;

		void render()
		{
			Font* font = Fonts::getFont("assets/fonts/Minecraft.ttf", 16_px);
			if (font)
			{
				float textScale = 0.4f;
				float textHzPadding = 0.1f;
				Style transparentSquare = Styles::defaultStyle;
				transparentSquare.color = "#00000055"_hex;

				// Draw first row of statistics
				glm::vec2 drawCallPos = glm::vec2(-2.95f, 1.35f);
				std::string drawCallStr = std::string("Draw calls: " + std::to_string(DebugStats::numDrawCalls));
				Renderer::drawString(
					drawCallStr,
					*font,
					drawCallPos,
					textScale,
					Styles::defaultStyle);

				glm::vec2 fpsPos = glm::vec2(-2.1f, 1.35f);
				std::string fpsStr = std::string("FPS: " + CMath::toString(1.0f / DebugStats::lastFrameTime));
				Renderer::drawString(
					fpsStr,
					*font,
					fpsPos,
					textScale,
					Styles::defaultStyle);

				glm::vec2 frameTimePos = glm::vec2(-1.48f, 1.35f);;
				std::string frameTimeStr = std::string("1/FPS: " + CMath::toString(DebugStats::lastFrameTime, 4));
				Renderer::drawString(
					frameTimeStr,
					*font,
					frameTimePos,
					textScale,
					Styles::defaultStyle);

				Renderer::drawFilledSquare2D(drawCallPos - glm::vec2(0.02f, 0.01f), glm::vec2(2.2f, 0.1f), transparentSquare, -1);

				// Draw second row of statistics
				glm::vec2 playerPosPos = glm::vec2(-2.95f, 1.23f);
				std::string playerPosStr = std::string("Player Pos: " + CMath::toString(DebugStats::playerPos));
				Renderer::drawString(
					playerPosStr,
					*font,
					playerPosPos,
					textScale,
					Styles::defaultStyle);

				glm::vec2 playerPosChunkCoordsPos = glm::vec2(-0.88f, 1.23f);
				glm::ivec2 chunkPos = World::toChunkCoords(DebugStats::playerPos);
				std::string playerPosChunkCoordsStr = std::string("Chunk: " + CMath::toString(chunkPos));
				Renderer::drawString(
					playerPosChunkCoordsStr,
					*font,
					playerPosChunkCoordsPos,
					textScale,
					Styles::defaultStyle);

				glm::vec2 playerOrientationPos = glm::vec2(0.05f, 1.23f);
				std::string playerOrientationStr = std::string("Rot: " + CMath::toString(DebugStats::playerOrientation));
				Renderer::drawString(
					playerOrientationStr,
					*font,
					playerOrientationPos,
					textScale,
					Styles::defaultStyle);

				Renderer::drawFilledSquare2D(playerPosPos - glm::vec2(0.02f, 0.01f), glm::vec2(5.1f, 0.1f), transparentSquare, -1);

				// Draw third row of statistics
				playerPosPos = glm::vec2(-2.95f, 1.11f);
				playerPosStr = std::string("MaxVertCount: " + CMath::toString(DebugStats::maxVertCount));
				Renderer::drawString(
					playerPosStr,
					*font,
					playerPosPos,
					textScale,
					Styles::defaultStyle);

				playerPosChunkCoordsPos = glm::vec2(-1.48f, 1.11f);
				playerPosChunkCoordsStr = std::string("MinVertCount: " + CMath::toString(DebugStats::minVertCount));
				Renderer::drawString(
					playerPosChunkCoordsStr,
					*font,
					playerPosChunkCoordsPos,
					textScale,
					Styles::defaultStyle);

				playerOrientationPos = glm::vec2(-0.28f, 1.11f);
				playerOrientationStr = std::string("AvgVertCount: " + CMath::toString(DebugStats::avgVertCount));
				Renderer::drawString(
					playerOrientationStr,
					*font,
					playerOrientationPos,
					textScale,
					Styles::defaultStyle);

				Renderer::drawFilledSquare2D(playerPosPos - glm::vec2(0.02f, 0.01f), glm::vec2(5.1f, 0.1f), transparentSquare, -1);
			}
		}
	}
}