#ifndef MINECRAFT_RENDERER_H
#define MINECRAFT_RENDERER_H
#include "core.h"
#include "core/Ecs.h"

namespace Minecraft
{
	struct Framebuffer;
	struct Style;
	struct Texture;
	struct Shader;
	struct Camera;

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
		void init(Ecs::Registry& registry);
		void render();
		void renderFramebuffer(const Framebuffer& framebuffer);

		// 2D Rendering stuff
		void drawSquare2D(const glm::vec2& start, const glm::vec2& size, const Style& style);
		void drawFilledSquare2D(const glm::vec2& start, const glm::vec2& size, const Style& style);
		void drawLine2D(const glm::vec2& start, const glm::vec2& end, const Style& style);
		void drawFilledCircle2D(const glm::vec2& position, float radius, int numSegments, const Style& style);
		void drawFilledTriangle2D(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const Style& style);
		void drawTexture2D(const RenderableTexture& renderable, const Style& color);
		//void drawString(const std::string& string, const Font& font, const glm::vec2& position, float scale, const glm::vec4& color);

		// 3D Rendering stuff
		void drawLine(const glm::vec3& start, const glm::vec3& end, const Style& style);
		void drawBox(const glm::vec3& center, const glm::vec3& size, const Style& style);

		void setShader2D(const Shader& shader);
		void setShader(const Shader& shader);
		void setCamera(const Camera& camera);

		void flushBatch2D();
		void flushBatch();
		void clearColor(const glm::vec4& color);
	}
}

#endif