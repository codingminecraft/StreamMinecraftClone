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
		uint32 numDrawCalls = 0;
		float lastFrameTime = 0.16f;
		glm::vec3 playerPos = glm::vec3();
		glm::vec3 playerOrientation = glm::vec3();
		std::atomic<float> totalChunkRamUsed = 0.0f;
		float totalChunkRamAvailable = 0.0f;
		Block blockLookingAt = BlockMap::NULL_BLOCK;
		Block airBlockLookingAt = BlockMap::NULL_BLOCK;

		void render()
		{
			Font* font = Fonts::getFont("assets/fonts/Minecraft.ttf", 16_px);
			if (font)
			{
				float textScale = 0.0015f;
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
				playerPosStr = std::string("Chunk RAM: " + 
					CMath::toString(DebugStats::totalChunkRamUsed / (1024.0f * 1024.0f)) + 
					std::string("/") + 
					CMath::toString(DebugStats::totalChunkRamAvailable / (1024.0f * 1024.0f)) +
					std::string("MB"));
				Renderer::drawString(
					playerPosStr,
					*font,
					playerPosPos,
					textScale,
					Styles::defaultStyle);

				glm::vec2 blockDataPos = glm::vec2(-1.45f, 1.11f);
				playerPosStr = std::string("Block ID: " + std::to_string(DebugStats::blockLookingAt.id));
				Renderer::drawString(
					playerPosStr,
					*font,
					blockDataPos,
					textScale,
					Styles::defaultStyle);

				blockDataPos = glm::vec2(-0.87f, 1.11f);
				playerPosStr = std::string("Light Level: " + std::to_string(DebugStats::airBlockLookingAt.calculatedLightLevel()));
				Renderer::drawString(
					playerPosStr,
					*font,
					blockDataPos,
					textScale,
					Styles::defaultStyle);

				blockDataPos = glm::vec2(-0.04f, 1.11f);
				playerPosStr = std::string("Sky Level: " + std::to_string(DebugStats::airBlockLookingAt.calculatedSkyLightLevel()));
				Renderer::drawString(
					playerPosStr,
					*font,
					blockDataPos,
					textScale,
					Styles::defaultStyle);

				Renderer::drawFilledSquare2D(playerPosPos - glm::vec2(0.02f, 0.01f), glm::vec2(5.1f, 0.1f), transparentSquare, -1);
			}
			else
			{
				g_logger_warning("Could not find font for debug stat rendering.");
			}
		}
	}
}