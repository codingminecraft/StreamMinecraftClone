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
	struct Sprite;
	struct TextureFormat;
	class Frustum;

	struct VoxelVertex
	{
		glm::vec3 position;
		glm::u8vec4 color;
	};

	struct DrawArraysIndirectCommand
	{
		uint32 count;
		uint32 instanceCount;
		uint32 first;
		uint32 baseInstance;
	};

	namespace Renderer
	{
		void init(Ecs::Registry& registry);
		void free();
		void render();

		void reloadShaders();

		void flushBatches2D();
		void flushBatches3D();
		void flushVoxelBatches();
		void flushBatches3D(const glm::mat4& projectionMatrix, const glm::mat4& viewMatrix);

		void setShader2D(const Shader& shader);
		void setShader(const Shader& shader);
		void setCamera(const Camera& camera);
		void setCameraFrustum(const Frustum& cameraFrustum);

		// 2D Rendering stuff
		void drawSquare2D(const glm::vec2& start, const glm::vec2& size, const Style& style, int zIndex = 0);
		void drawFilledSquare2D(const glm::vec2& start, const glm::vec2& size, const Style& style, int zIndex = 0);
		void drawLine2D(const glm::vec2& start, const glm::vec2& end, const Style& style, int zIndex = 0);
		void drawFilledCircle2D(const glm::vec2& position, float radius, int numSegments, const Style& style, int zIndex = 0);
		void drawFilledTriangle2D(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const Style& style, int zIndex = 0);
		void drawTexture2D(const Sprite& sprite, const glm::vec2& position, const glm::vec2& size, const Style& color, int zIndex = 0, bool isFont = false);
		void drawString(const std::string& string, const Font& font, const glm::vec2& position, float scale, const Style& style, int zIndex = 0);

		// 3D Rendering stuff
		void draw3DModel(const glm::vec3& position, const glm::vec3& scale, float rotation, const VoxelVertex* vertices, int verticesLength);
		void drawLine(const glm::vec3& start, const glm::vec3& end, const Style& style);
		void drawBox(const glm::vec3& center, const glm::vec3& size, const Style& style);
		void drawTexturedCube(const glm::vec3& center, const glm::vec3& size, const TextureFormat& sideSprite, const TextureFormat& topSprite, const TextureFormat& bottomSprite, float rotation = 0.0f);
	}
}

#endif