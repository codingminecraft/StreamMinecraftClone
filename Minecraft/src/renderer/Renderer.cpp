#include "renderer/Renderer.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Framebuffer.h"
#include "renderer/Camera.h"
#include "renderer/Styles.h"
#include "renderer/Font.h"
#include "renderer/Batch.hpp"
#include "world/World.h"
#include "core/Components.h"
#include "core/Application.h"
#include "core/Window.h"
#include "utils/DebugStats.h"

namespace Minecraft
{
	namespace Renderer
	{
		// Internal variables		
		static std::vector<Batch<RenderVertex2D>> batches2D;
		static Batch<RenderVertex> batch;

		static Shader shader2D;
		static Shader shader;

		static Ecs::Registry* registry;
		static const Camera* camera;

		// Internal functions
		static Batch<RenderVertex2D>& getBatch2D(int zIndex, const Texture& texture, bool useTexture);
		static Batch<RenderVertex2D>& createBatch2D(int zIndex);
		static void GLAPIENTRY messageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam);
		static void drawTexturedTriangle2D(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, const Texture* texture, const Style& style, int zIndex);

		void init(Ecs::Registry& sceneRegistry)
		{
			registry = &sceneRegistry;
			camera = nullptr;
			batch.numVertices = 0;

			// Load OpenGL functions using Glad
			if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
			{
				g_logger_error("Failed to initialize glad.");
				return;
			}
			g_logger_info("GLAD initialized.");
			g_logger_info("Hello OpenGL %d.%d", GLVersion.major, GLVersion.minor);

#ifdef _DEBUG
			glEnable(GL_DEBUG_OUTPUT);
			glDebugMessageCallback(messageCallback, 0);
#endif

			// Enable render parameters
			glEnable(GL_DEPTH_TEST);
			glEnable(GL_CULL_FACE);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			// Initialize default shader
			shader2D.compile("assets/shaders/DebugShader2D.glsl");
			shader.compile("assets/shaders/DebugShader3D.glsl");

			batch.init(
				{
					{0, 3, AttributeType::Float, offsetof(RenderVertex, start)},
					{1, 3, AttributeType::Float, offsetof(RenderVertex, end)},
					{2, 1, AttributeType::Float, offsetof(RenderVertex, isStart)},
					{3, 1, AttributeType::Float, offsetof(RenderVertex, direction)},
					{4, 1, AttributeType::Float, offsetof(RenderVertex, strokeWidth)},
					{5, 4, AttributeType::Float, offsetof(RenderVertex, color)}
				}
			);
		}

		void free()
		{
			for (Batch<RenderVertex2D> batch2D : batches2D)
			{
				batch2D.free();
			}
			batch.free();
		}

		void render()
		{
			flushBatches3D();
			flushBatches2D();
		}

		void flushBatches2D()
		{
			glDisable(GL_CULL_FACE);

			shader2D.bind();
			shader2D.uploadMat4("uProjection", camera->calculateHUDProjectionMatrix());
			shader2D.uploadMat4("uView", camera->calculateHUDViewMatrix());

			for (Batch<RenderVertex2D>& batch2D : batches2D)
			{
				if (batch2D.numVertices <= 0)
				{
					return;
				}

				for (int i = 0; i < batch.textureGraphicsIds.size(); i++)
				{
					if (batch2D.textureGraphicsIds[i] != UINT32_MAX)
					{
						glActiveTexture(GL_TEXTURE0 + i);
						glBindTexture(GL_TEXTURE_2D, batch2D.textureGraphicsIds[i]);
					}
				}
				shader2D.uploadIntArray("uFontTextures[0]", _Batch::textureIndices().size(), _Batch::textureIndices().data());
				shader2D.uploadInt("uZIndex", batch2D.zIndex);

				batch2D.flush();

				DebugStats::numDrawCalls++;
			}

			glEnable(GL_CULL_FACE);
		}

		void flushBatches3D()
		{
			if (batch.numVertices <= 0)
			{
				return;
			}

			glDisable(GL_CULL_FACE);

			shader.bind();
			shader.uploadMat4("uProjection", camera->calculateProjectionMatrix(*registry));
			shader.uploadMat4("uView", camera->calculateViewMatrix(*registry));
			shader.uploadFloat("uAspectRatio", Application::getWindow().getAspectRatio());

			batch.flush();

			glEnable(GL_CULL_FACE);

			DebugStats::numDrawCalls++;
		}

		void setShader2D(const Shader& newShader)
		{
			shader2D = newShader;
		}

		void setShader(const Shader& newShader)
		{
			shader = newShader;
		}

		void setCamera(const Camera& cameraRef)
		{
			camera = &cameraRef;
		}

		void clearColor(const glm::vec4& color)
		{
			glClearColor(color.r, color.g, color.b, color.a);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		}

		// =========================================================
		// Draw 2D Functions
		// =========================================================
		void drawSquare2D(const glm::vec2& start, const glm::vec2& size, const Style& style, int zIndex)
		{
			drawLine2D(start, start + glm::vec2{ size.x, 0 }, style, zIndex);
			drawLine2D(start + glm::vec2{ 0, size.y }, start + size, style, zIndex);
			drawLine2D(start, start + glm::vec2{ 0, size.y }, style, zIndex);
			drawLine2D(start + glm::vec2{ size.x, 0 }, start + size, style, zIndex);
		}

		void drawFilledSquare2D(const glm::vec2& start, const glm::vec2& size, const Style& style, int zIndex)
		{
			drawFilledTriangle2D(start, start + size, start + glm::vec2{ 0, size.y }, style, zIndex);
			drawFilledTriangle2D(start, start + glm::vec2{ size.x, 0 }, start + size, style, zIndex);
		}

		void drawLine2D(const glm::vec2& start, const glm::vec2& end, const Style& style, int zIndex)
		{
			// Draw the line
			glm::vec2 direction = end - start;
			glm::vec2 normalDirection = glm::normalize(direction);
			glm::vec2 perpVector = glm::normalize(glm::vec2{ normalDirection.y, -normalDirection.x });

			glm::vec2 v0 = start + (perpVector * style.strokeWidth * 0.5f);
			glm::vec2 v1 = v0 + direction;
			glm::vec2 v2 = v1 - (perpVector * style.strokeWidth);
			drawFilledTriangle2D(v0, v1, v2, style, zIndex);

			glm::vec2 v3 = v0 - (perpVector * style.strokeWidth);
			drawFilledTriangle2D(v0, v2, v3, style, zIndex);

			// Draw the cap type
			if (style.lineEnding == CapType::Arrow)
			{
				// Add arrow tip
				glm::vec2 centerDot = end + (normalDirection * style.strokeWidth * 0.5f);
				glm::vec2 vectorToCenter = glm::normalize(centerDot - (end - perpVector * style.strokeWidth * 0.5f));
				glm::vec2 oVectorToCenter = glm::normalize(centerDot - (end + perpVector * style.strokeWidth * 0.5f));
				glm::vec2 bottomLeft = centerDot - vectorToCenter * style.strokeWidth * 4.0f;
				glm::vec2 bottomRight = centerDot - oVectorToCenter * style.strokeWidth * 4.0f;
				glm::vec2 top = centerDot + normalDirection * style.strokeWidth * 4.0f;

				drawFilledTriangle2D(centerDot, bottomLeft, top, style, zIndex);
				drawFilledTriangle2D(top, centerDot, bottomRight, style, zIndex);
				drawFilledTriangle2D(centerDot, end + perpVector * style.strokeWidth * 0.5f, end - perpVector * style.strokeWidth * 0.5f, style, zIndex);
			}
		}

		void drawFilledCircle2D(const glm::vec2& position, float radius, int numSegments, const Style& style, int zIndex)
		{
			float t = 0;
			float sectorSize = 360.0f / (float)numSegments;
			for (int i = 0; i < numSegments; i++)
			{
				float x = radius * glm::cos(glm::radians(t));
				float y = radius * glm::sin(glm::radians(t));
				float nextT = t + sectorSize;
				float nextX = radius * glm::cos(glm::radians(nextT));
				float nextY = radius * glm::sin(glm::radians(nextT));

				drawFilledTriangle2D(position, position + glm::vec2{ x, y }, position + glm::vec2{ nextX, nextY }, style, zIndex);

				t += sectorSize;
			}
		}

		void drawFilledTriangle2D(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const Style& style, int zIndex)
		{
			Batch<RenderVertex2D>& batch2D = getBatch2D(zIndex, {}, false);
			if (batch2D.numVertices + 3 >= _Batch::maxBatchSize)
			{
				batch2D = createBatch2D(zIndex);
			}

			RenderVertex2D v;
			v.position = p0;
			v.color = style.color;
			v.textureSlot = 0;
			batch2D.addVertex(v);

			v.position = p1;
			v.color = style.color;
			v.textureSlot = 0;
			batch2D.addVertex(v);

			v.position = p2;
			v.color = style.color;
			v.textureSlot = 0;
			batch2D.addVertex(v);
		}

		void drawTexture2D(const RenderableTexture& renderable, const Style& style, int zIndex)
		{
			glm::vec2 v0 = renderable.start;
			glm::vec2 v1 = renderable.start + glm::vec2{ 0, renderable.size.y };
			glm::vec2 v2 = renderable.start + renderable.size;
			glm::vec2 v3 = renderable.start + glm::vec2{ renderable.size.x, 0 };

			glm::vec2 uv0 = renderable.texCoordStart;
			glm::vec2 uv1 = renderable.texCoordStart + glm::vec2{ 0, renderable.texCoordSize.y };
			glm::vec2 uv2 = renderable.texCoordStart + renderable.texCoordSize;
			glm::vec2 uv3 = renderable.texCoordStart + glm::vec2{ renderable.texCoordSize.x, 0 };

			drawTexturedTriangle2D(
				v0,
				v2,
				v1,
				uv0,
				uv2,
				uv1,
				renderable.texture,
				style,
				zIndex
			);
			drawTexturedTriangle2D(
				v0,
				v3,
				v2,
				uv0,
				uv3,
				uv2,
				renderable.texture,
				style,
				zIndex
			);
		}

		void drawString(const std::string& string, const Font& font, const glm::vec2& position, float scale, const Style& style, int zIndex)
		{
			float x = position.x;
			float y = position.y;

			for (int i = 0; i < string.length(); i++)
			{
				char c = string[i];
				RenderableChar renderableChar = font.getCharInfo(c);
				float charWidth = renderableChar.texCoordSize.x * font.fontSize * scale;
				float charHeight = renderableChar.texCoordSize.y * font.fontSize * scale;
				float adjustedY = y - renderableChar.bearingY * font.fontSize * scale;

				drawTexture2D(RenderableTexture{
					&font.texture,
					{ x, adjustedY },
					{ charWidth, charHeight },
					renderableChar.texCoordStart,
					renderableChar.texCoordSize
					}, style, zIndex);

				char nextC = i < string.length() - 1 ? string[i + 1] : '\0';
				//x += font.getKerning(c, nextC) * scale * font.fontSize;
				x += renderableChar.advance.x * scale * font.fontSize;
			}
		}

		// =========================================================
		// Draw 3D Functions
		// ===================================================
		void drawLine(const glm::vec3& start, const glm::vec3& end, const Style& style)
		{
			if (batch.numVertices + 6 >= _Batch::maxBatchSize)
			{
				flushBatches3D();
			}

			// First triangle
			RenderVertex v;
			v.isStart = 1.0f;
			v.start = start;
			v.end = end;
			v.direction = -1.0f;
			v.color = style.color;
			v.strokeWidth = style.strokeWidth;
			batch.addVertex(v);

			v.isStart = 1.0f;
			v.start = start;
			v.end = end;
			v.direction = 1.0f;
			v.color = style.color;
			v.strokeWidth = style.strokeWidth;
			batch.addVertex(v);

			v.isStart = 0.0f;
			v.start = start;
			v.end = end;
			v.direction = 1.0f;
			v.color = style.color;
			v.strokeWidth = style.strokeWidth;
			batch.addVertex(v);

			// Second triangle
			v.isStart = 1.0f;
			v.start = start;
			v.end = end;
			v.direction = -1.0f;
			v.color = style.color;
			v.strokeWidth = style.strokeWidth;
			batch.addVertex(v);

			v.isStart = 0.0f;
			v.start = start;
			v.end = end;
			v.direction = 1.0f;
			v.color = style.color;
			v.strokeWidth = style.strokeWidth;
			batch.addVertex(v);

			v.isStart = 0.0f;
			v.start = start;
			v.end = end;
			v.direction = -1.0f;
			v.color = style.color;
			v.strokeWidth = style.strokeWidth;
			batch.addVertex(v);
		}

		void drawBox(const glm::vec3& center, const glm::vec3& size, const Style& style)
		{
			glm::vec3 v0 = center - (size * 0.5f);
			glm::vec3 v1 = v0 + glm::vec3(size.x, 0, 0);
			glm::vec3 v2 = v0 + glm::vec3(0, 0, size.z);
			glm::vec3 v3 = v0 + glm::vec3(size.x, 0, size.z);

			glm::vec3 v4 = v0 + glm::vec3(0, size.y, 0);
			glm::vec3 v5 = v1 + glm::vec3(0, size.y, 0);
			glm::vec3 v6 = v2 + glm::vec3(0, size.y, 0);
			glm::vec3 v7 = v3 + glm::vec3(0, size.y, 0);

			drawLine(v0, v1, style);
			drawLine(v0, v2, style);
			drawLine(v2, v3, style);
			drawLine(v1, v3, style);

			drawLine(v4, v5, style);
			drawLine(v4, v6, style);
			drawLine(v5, v7, style);
			drawLine(v6, v7, style);

			drawLine(v0, v4, style);
			drawLine(v1, v5, style);
			drawLine(v2, v6, style);
			drawLine(v3, v7, style);
		}

		// =========================================================
		// Internal Functions
		// =========================================================
		static void GLAPIENTRY
			messageCallback(GLenum source,
				GLenum type,
				GLuint id,
				GLenum severity,
				GLsizei length,
				const GLchar* message,
				const void* userParam)
		{
			if (type == GL_DEBUG_TYPE_ERROR)
			{
				g_logger_error("GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s",
					(type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
					type, severity, message);

				GLenum err;
				while ((err = glGetError()) != GL_NO_ERROR)
				{
					g_logger_error("Error Code: 0x%8x", err);
				}
			}
		}

		static void drawTexturedTriangle2D(
			const glm::vec2& p0,
			const glm::vec2& p1,
			const glm::vec2& p2,
			const glm::vec2& uv0,
			const glm::vec2& uv1,
			const glm::vec2& uv2,
			const Texture* texture,
			const Style& style,
			int zIndex)
		{
			Batch<RenderVertex2D>& batch2D = getBatch2D(zIndex, *texture, true);
			if (batch2D.numVertices + 3 >= _Batch::maxBatchSize)
			{
				batch2D = createBatch2D(zIndex);
			}

			uint32 texSlot = batch2D.getTextureSlot(texture->graphicsId);

			// One triangle per sector
			// TODO: Why is this creating a memory leak???
			RenderVertex2D v;
			v.position = p0;
			v.color = style.color;
			v.textureSlot = texSlot;
			v.textureCoords = uv0;
			batch2D.addVertex(v);

			v.position = p1;
			v.color = style.color;
			v.textureSlot = texSlot;
			v.textureCoords = uv1;
			batch2D.addVertex(v);

			v.position = p2;
			v.color = style.color;
			v.textureSlot = texSlot;
			v.textureCoords = uv2;
			batch2D.addVertex(v);
		}

		static Batch<RenderVertex2D>& getBatch2D(int zIndex, const Texture& texture, bool useTexture)
		{
			for (Batch<RenderVertex2D>& batch : batches2D)
			{
				if (batch.hasRoom() && batch.zIndex == zIndex && 
					(!useTexture || batch.hasTexture(texture.graphicsId) || batch.hasTextureRoom()))
				{
					return batch;
				}
			}

			return createBatch2D(zIndex);
		}

		static Batch<RenderVertex2D>& createBatch2D(int zIndex)
		{
			// No batch found, create a new one and sort the batches
			Batch<RenderVertex2D> newBatch;
			newBatch.init(
				{
					{0, 2, AttributeType::Float, offsetof(RenderVertex2D, position)},
					{1, 4, AttributeType::Float, offsetof(RenderVertex2D, color)},
					{2, 1, AttributeType::Uint, offsetof(RenderVertex2D, textureSlot)},
					{3, 2, AttributeType::Float, offsetof(RenderVertex2D, textureCoords)}
				}
			);
			newBatch.zIndex = zIndex;
			batches2D.push_back(newBatch);
			std::sort(batches2D.begin(), batches2D.end(),
				[](const Batch<RenderVertex2D>& a, const Batch<RenderVertex2D>& b)
				{
					return a.zIndex < b.zIndex;
				});

			// Since we added stuff to the vector and everything recursively call this function
			// so that we get the appropriate reference
			return getBatch2D(zIndex, {}, false);
		}
	}
}