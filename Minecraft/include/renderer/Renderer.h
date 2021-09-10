#ifndef MINECRAFT_RENDERER_H
#define MINECRAFT_RENDERER_H
#include "core.h"

namespace Minecraft
{
	struct Framebuffer;
	struct Style;
	struct Texture;

	struct RenderableTexture
	{
		const Texture* texture;
		glm::vec2 start;
		glm::vec2 size;
		glm::vec2 texCoordStart;
		glm::vec2 texCoordSize;
	};

	namespace Renderer
	{
		void init();
		void render();
		void renderFramebuffer(const Framebuffer& framebuffer);
		void drawSquare(const glm::vec2& start, const glm::vec2& size, const Style& style);
		void drawFilledSquare(const glm::vec2& start, const glm::vec2& size, const Style& style);
		void drawLine(const glm::vec2& start, const glm::vec2& end, const Style& style);
		void drawFilledCircle(const glm::vec2& position, float radius, int numSegments, const Style& style);
		void drawFilledTriangle(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const Style& style);
		void drawTexture(const RenderableTexture& renderable, const Style& color);
		//void drawString(const std::string& string, const Font& font, const glm::vec2& position, float scale, const glm::vec4& color);
		void flushBatch();
		void clearColor(const glm::vec4& color);
	}
}

#endif