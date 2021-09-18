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
	struct Font;

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
		void free();
		void render();

		void flushBatches2D();
		void flushBatches3D();

		void clearColor(const glm::vec4& color);
		void setShader2D(const Shader& shader);
		void setShader(const Shader& shader);
		void setCamera(const Camera& camera);

		// 2D Rendering stuff
		void drawSquare2D(const glm::vec2& start, const glm::vec2& size, const Style& style, int zIndex = 0);
		void drawFilledSquare2D(const glm::vec2& start, const glm::vec2& size, const Style& style, int zIndex = 0);
		void drawLine2D(const glm::vec2& start, const glm::vec2& end, const Style& style, int zIndex = 0);
		void drawFilledCircle2D(const glm::vec2& position, float radius, int numSegments, const Style& style, int zIndex = 0);
		void drawFilledTriangle2D(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const Style& style, int zIndex = 0);
		void drawTexture2D(const RenderableTexture& renderable, const Style& color, int zIndex = 0);
		void drawString(const std::string& string, const Font& font, const glm::vec2& position, float scale, const Style& style, int zIndex = 0);

		// 3D Rendering stuff
		void drawLine(const glm::vec3& start, const glm::vec3& end, const Style& style);
		void drawBox(const glm::vec3& center, const glm::vec3& size, const Style& style);
	}
}

#endif