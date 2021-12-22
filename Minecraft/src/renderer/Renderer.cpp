#include "renderer/Renderer.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Framebuffer.h"
#include "renderer/Camera.h"
#include "renderer/Styles.h"
#include "renderer/Font.h"
#include "renderer/Batch.hpp"
#include "renderer/Sprites.h"
#include "renderer/Frustum.h"
#include "world/BlockMap.h"
#include "world/World.h"
#include "core/Components.h"
#include "core/Application.h"
#include "core/Window.h"
#include "utils/DebugStats.h"
#include "utils/CMath.h"

namespace Minecraft
{
	namespace Renderer
	{
		// Internal variables		
		static std::vector<Batch<RenderVertex2D>> batches2D;
		static Batch<RenderVertexLine> batch3DLines;
		static Batch<RenderVertex3D> batch3DRegular;
		static Batch<VoxelVertex> batch3DVoxels;

		static Shader shader2D;
		static Shader line3DShader;
		static Shader regular3DShader;
		static Shader batch3DVoxelsShader;

		static Ecs::Registry* registry;
		static const Camera* camera;
		static const Frustum* cameraFrustum;

		// Internal functions
		static Batch<RenderVertex2D>& getBatch2D(int zIndex, const Texture& texture, bool useTexture, bool isFont);
		static Batch<RenderVertex2D>& createBatch2D(int zIndex, bool isFont);
		static void GLAPIENTRY messageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam);
		static void drawTexturedTriangle2D(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, const Texture* texture, const Style& style, int zIndex, bool isFont = false);
		static void drawTexturedTriangle3D(const glm::vec4& p0, const glm::vec4& p1, const glm::vec4& p2, const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec3& normal, const Texture* texture);

		void init(Ecs::Registry& sceneRegistry)
		{
			registry = &sceneRegistry;
			camera = nullptr;
			batch3DLines.numVertices = 0;

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
			glCullFace(GL_BACK);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			// Initialize default line3DShader
			shader2D.compile("assets/shaders/DebugShader2D.glsl");
			line3DShader.compile("assets/shaders/DebugShader3D.glsl");
			regular3DShader.compile("assets/shaders/RegularShader3D.glsl");
			batch3DVoxelsShader.compile("assets/shaders/VoxelShader.glsl");

			batch3DLines.init(
				{
					{0, 3, AttributeType::Float, offsetof(RenderVertexLine, start)},
					{1, 3, AttributeType::Float, offsetof(RenderVertexLine, end)},
					{2, 1, AttributeType::Float, offsetof(RenderVertexLine, isStart)},
					{3, 1, AttributeType::Float, offsetof(RenderVertexLine, direction)},
					{4, 1, AttributeType::Float, offsetof(RenderVertexLine, strokeWidth)},
					{5, 4, AttributeType::Float, offsetof(RenderVertexLine, color)}
				}
			);
			batch3DRegular.init(
				{
					{0, 3, AttributeType::Float, offsetof(RenderVertex3D, position)},
					{1, 1, AttributeType::Uint, offsetof(RenderVertex3D, textureSlot)},
					{2, 2, AttributeType::Float, offsetof(RenderVertex3D, textureCoords)},
					{3, 3, AttributeType::Float, offsetof(RenderVertex3D, normal)}
				}
			);
			batch3DVoxels.init(
				{
					{0, 3, AttributeType::Float, offsetof(VoxelVertex, position)},
					{1, 1, AttributeType::Uint, offsetof(VoxelVertex, color)},
				}
			);
			g_logger_info("Initializing the 3D debug batch3DLines succeeded.");
		}

		void free()
		{
			for (Batch<RenderVertex2D> batch2D : batches2D)
			{
				batch2D.free();
			}
			batch3DLines.free();
			batch3DRegular.free();
			batch3DVoxels.free();

			shader2D.destroy();
			line3DShader.destroy();
			regular3DShader.destroy();
			batch3DVoxelsShader.destroy();
		}

		void render()
		{
			flushBatches3D();
			flushBatches2D();
			flushVoxelBatches();
		}

		void reloadShaders()
		{
			shader2D.destroy();
			line3DShader.destroy();
			regular3DShader.destroy();
			batch3DVoxelsShader.destroy();
			shader2D.compile("assets/shaders/DebugShader2D.glsl");
			line3DShader.compile("assets/shaders/DebugShader3D.glsl");
			regular3DShader.compile("assets/shaders/RegularShader3D.glsl");
			batch3DVoxelsShader.compile("assets/shaders/VoxelShader.glsl");
		}

		void flushBatches2D()
		{
			glDisable(GL_CULL_FACE);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			std::sort(batches2D.begin(), batches2D.end());
			shader2D.bind();
			shader2D.uploadMat4("uProjection", camera->calculateHUDProjectionMatrix());
			shader2D.uploadMat4("uView", camera->calculateHUDViewMatrix());

			for (Batch<RenderVertex2D>& batch2D : batches2D)
			{
				if (batch2D.numVertices <= 0)
				{
					batch2D.flush();
					continue;
				}

				for (int i = 0; i < batch2D.textureGraphicsIds.size(); i++)
				{
					if (batch2D.textureGraphicsIds[i] != UINT32_MAX)
					{
						glBindTextureUnit(i, batch2D.textureGraphicsIds[i]);
					}
				}
				shader2D.uploadIntArray("uFontTextures[0]", 8, _Batch::textureIndices().data());
				shader2D.uploadIntArray("uTextures[0]", 8, _Batch::textureIndices().data() + 8);
				shader2D.uploadInt("uZIndex", batch2D.zIndex);

				batch2D.flush();

				DebugStats::numDrawCalls++;
			}

			glEnable(GL_CULL_FACE);
		}

		void flushBatches3D()
		{
			if (batch3DLines.numVertices <= 0)
			{
				return;
			}

			glDisable(GL_CULL_FACE);

			line3DShader.bind();
			line3DShader.uploadMat4("uProjection", camera->calculateProjectionMatrix(*registry));
			line3DShader.uploadMat4("uView", camera->calculateViewMatrix(*registry));
			line3DShader.uploadFloat("uAspectRatio", Application::getWindow().getAspectRatio());

			batch3DLines.flush();

			glEnable(GL_CULL_FACE);

			regular3DShader.bind();
			regular3DShader.uploadMat4("uProjection", camera->calculateProjectionMatrix(*registry));
			regular3DShader.uploadMat4("uView", camera->calculateViewMatrix(*registry));
			for (int i = 0; i < batch3DRegular.textureGraphicsIds.size(); i++)
			{
				if (batch3DRegular.textureGraphicsIds[i] != UINT32_MAX)
				{
					glBindTextureUnit(i, batch3DRegular.textureGraphicsIds[i]);
				}
			}
			regular3DShader.uploadIntArray("uTextures[0]", 16, _Batch::textureIndices().data());

			batch3DRegular.flush();

			DebugStats::numDrawCalls += 2;
		}

		void flushVoxelBatches()
		{
			if (batch3DVoxels.numVertices <= 0)
			{
				return;
			}

			glEnable(GL_CULL_FACE);

			batch3DVoxelsShader.bind();
			batch3DVoxelsShader.uploadMat4("uProjection", camera->calculateProjectionMatrix(*registry));
			batch3DVoxelsShader.uploadMat4("uView", camera->calculateViewMatrix(*registry));

			batch3DVoxels.flush();

			DebugStats::numDrawCalls += 1;
		}

		void flushBatches3D(const glm::mat4& projectionMatrix, const glm::mat4& viewMatrix)
		{
			regular3DShader.bind();
			regular3DShader.uploadMat4("uProjection", projectionMatrix);
			regular3DShader.uploadMat4("uView", viewMatrix);
			for (int i = 0; i < batch3DRegular.textureGraphicsIds.size(); i++)
			{
				if (batch3DRegular.textureGraphicsIds[i] != UINT32_MAX)
				{
					glBindTextureUnit(i, batch3DRegular.textureGraphicsIds[i]);
				}
			}
			regular3DShader.uploadIntArray("uTextures[0]", 16, _Batch::textureIndices().data());

			batch3DRegular.flush();
		}

		void setShader2D(const Shader& newShader)
		{
			shader2D = newShader;
		}

		void setShader(const Shader& newShader)
		{
			line3DShader = newShader;
		}

		void setCamera(const Camera& cameraRef)
		{
			camera = &cameraRef;
		}

		void setCameraFrustum(const Frustum& cameraFrustumRef)
		{
			cameraFrustum = &cameraFrustumRef;
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
			Batch<RenderVertex2D>& batch2D = getBatch2D(zIndex, {}, false, false);
			if (batch2D.numVertices + 3 >= _Batch::maxBatchSize)
			{
				batch2D = createBatch2D(zIndex, false);
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

		void drawTexture2D(const Sprite& sprite, const glm::vec2& position, const glm::vec2& size, const Style& style, int zIndex, bool isFont)
		{
			glm::vec2 v0 = position;
			glm::vec2 v1 = position + glm::vec2{ 0, size.y };
			glm::vec2 v2 = position + size;
			glm::vec2 v3 = position + glm::vec2{ size.x, 0 };

			glm::vec2 uv0 = sprite.uvStart + glm::vec2{ 0, sprite.uvSize.y };
			glm::vec2 uv1 = sprite.uvStart;
			glm::vec2 uv2 = sprite.uvStart + glm::vec2{ sprite.uvSize.x, 0 };
			glm::vec2 uv3 = sprite.uvStart + sprite.uvSize;

			drawTexturedTriangle2D(
				v0,
				v2,
				v1,
				uv0,
				uv2,
				uv1,
				&sprite.texture,
				style,
				zIndex,
				isFont
			);
			drawTexturedTriangle2D(
				v0,
				v3,
				v2,
				uv0,
				uv3,
				uv2,
				&sprite.texture,
				style,
				zIndex,
				isFont
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
				float charWidth = renderableChar.charSize.x * scale;
				float charHeight = renderableChar.charSize.y * scale;
				float adjustedY = y - (renderableChar.charSize.y - renderableChar.bearingY) * scale;

				drawTexture2D(Sprite{
					font.texture,
					renderableChar.texCoordStart,
					renderableChar.texCoordSize
					},
					{ x, adjustedY },
					{ charWidth, charHeight },
					style, zIndex, true);

				char nextC = i < string.length() - 1 ? string[i + 1] : '\0';
				x += font.getKerning(c, nextC) * scale;
				x += renderableChar.advance.x * scale;
			}
		}

		// =========================================================
		// Draw 3D Functions
		// ===================================================
		void draw3DModel(const glm::vec3& position, const glm::vec3& scale, float rotation, const VoxelVertex* vertices, int verticesLength)
		{
			for (int i = 0; i < verticesLength; i++)
			{
				if (batch3DVoxels.numVertices + 1 > _Batch::maxBatchSize)
				{
					flushVoxelBatches();
				}

				VoxelVertex vertex = vertices[i];
				vertex.position += position;
				batch3DVoxels.addVertex(vertex);
			}
		}

		void drawLine(const glm::vec3& start, const glm::vec3& end, const Style& style)
		{
			if (batch3DLines.numVertices + 6 >= _Batch::maxBatchSize)
			{
				flushBatches3D();
			}

			// First triangle
			RenderVertexLine v;
			v.isStart = 1.0f;
			v.start = start;
			v.end = end;
			v.direction = -1.0f;
			v.color = style.color;
			v.strokeWidth = style.strokeWidth;
			batch3DLines.addVertex(v);

			v.isStart = 1.0f;
			v.start = start;
			v.end = end;
			v.direction = 1.0f;
			v.color = style.color;
			v.strokeWidth = style.strokeWidth;
			batch3DLines.addVertex(v);

			v.isStart = 0.0f;
			v.start = start;
			v.end = end;
			v.direction = 1.0f;
			v.color = style.color;
			v.strokeWidth = style.strokeWidth;
			batch3DLines.addVertex(v);

			// Second triangle
			v.isStart = 1.0f;
			v.start = start;
			v.end = end;
			v.direction = -1.0f;
			v.color = style.color;
			v.strokeWidth = style.strokeWidth;
			batch3DLines.addVertex(v);

			v.isStart = 0.0f;
			v.start = start;
			v.end = end;
			v.direction = 1.0f;
			v.color = style.color;
			v.strokeWidth = style.strokeWidth;
			batch3DLines.addVertex(v);

			v.isStart = 0.0f;
			v.start = start;
			v.end = end;
			v.direction = -1.0f;
			v.color = style.color;
			v.strokeWidth = style.strokeWidth;
			batch3DLines.addVertex(v);
		}

		void drawBox(const glm::vec3& center, const glm::vec3& size, const Style& style)
		{
			// TODO: Do this in a better way... Maybe do sphere check before expensive box check
			if (!cameraFrustum->isBoxVisible(center - (size * 0.5f), center + (size * 0.5f)))
			{
				return;
			}

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

		void drawTexturedCube(const glm::vec3& center, const glm::vec3& size, const TextureFormat& sideSprite, const TextureFormat& topSprite, const TextureFormat& bottomSprite, float rotation)
		{
			glm::vec3 halfSize = size * 0.5f;
			// TODO: Do this in a better way... Maybe do sphere check before expensive box check
			if (cameraFrustum && !cameraFrustum->isBoxVisible(center - halfSize, center + halfSize))
			{
				return;
			}

			const glm::vec4 offsets[6] = {
				{1, 0, 0, 1},
				{-1, 0, 0, 1},
				{0, 1, 0, 1},
				{0, -1, 0, 1},
				{0, 0, 1, 1},
				{0, 0, -1, 1}
			};
			const glm::vec4 squareOffsets[6][4] = {
				{{0, 1, 1, 0}, {0, -1, 1, 0}, {0, -1, -1, 0}, {0, 1, -1, 0}}, // Triangle order for front face
				{{0, 1, -1, 0}, {0, -1, -1, 0}, {0, -1, 1, 0}, {0, 1, 1, 0}}, // Triangle order for back face
				{{1, 0, 1, 0}, {1, 0, -1, 0}, {-1, 0, -1, 0}, {-1, 0, 1, 0}}, // Triangle order for top face
				{{1, 0, 1, 0}, {-1, 0, 1, 0}, {-1, 0, -1, 0}, {1, 0, -1, 0}}, // Triangle order for bottom face
				{{-1, 1, 0, 0}, {-1, -1, 0, 0}, {1, -1, 0, 0}, {1, 1, 0, 0}}, // Triangle order for right face
				{{1, 1, 0, 0}, {1, -1, 0, 0}, {-1, -1, 0, 0}, {-1, 1, 0, 0}}  // Triangle order for left face
			};
			const TextureFormat* spriteToUse[6] = {
				&sideSprite,
				&sideSprite,
				&topSprite, &bottomSprite, &sideSprite, &sideSprite
			};
			const glm::mat4 transformMatrix =
				glm::rotate(
					glm::translate(glm::mat4(1.0f), center),
					glm::radians(rotation),
					glm::vec3(0, 1, 0)
				);
			const glm::vec4 center4 = glm::vec4(center, 1.0f);
			const glm::vec4 halfSize4 = glm::vec4(halfSize, 1.0f);

			// Six faces
			for (int i = 0; i < 6; i++)
			{
				const TextureFormat* sprite = spriteToUse[i];
				glm::vec4 offset = offsets[i];

				// Translate
				glm::vec4 p0 = halfSize4 * offset + halfSize4 * squareOffsets[i][0];
				glm::vec4 p1 = halfSize4 * offset + halfSize4 * squareOffsets[i][1];
				glm::vec4 p2 = halfSize4 * offset + halfSize4 * squareOffsets[i][2];
				glm::vec4 p3 = halfSize4 * offset + halfSize4 * squareOffsets[i][3];

				// Transform
				p0 = transformMatrix * p0;
				p1 = transformMatrix * p1;
				p2 = transformMatrix * p2;
				p3 = transformMatrix * p3;

				glm::vec2 uv0 = sprite->uvs[0];
				glm::vec2 uv1 = sprite->uvs[1];
				glm::vec2 uv2 = sprite->uvs[2];
				glm::vec2 uv3 = sprite->uvs[3];

				glm::vec3 normal = glm::vec3(offset.x, offset.y, offset.z);

				// Two triangles each face
				drawTexturedTriangle3D(p0, p1, p2, uv0, uv1, uv2, normal, sprite->texture);
				drawTexturedTriangle3D(p0, p2, p3, uv0, uv2, uv3, normal, sprite->texture);
			}
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
			int zIndex,
			bool isFont)
		{
			Batch<RenderVertex2D>& batch2D = getBatch2D(zIndex, *texture, true, isFont);
			if (batch2D.numVertices + 3 > _Batch::maxBatchSize)
			{
				batch2D = createBatch2D(zIndex, isFont);
			}

			uint32 texSlot = batch2D.getTextureSlot(texture->graphicsId, isFont);

			// One triangle per sector
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

		static void drawTexturedTriangle3D(
			const glm::vec4& p0,
			const glm::vec4& p1,
			const glm::vec4& p2,
			const glm::vec2& uv0,
			const glm::vec2& uv1,
			const glm::vec2& uv2,
			const glm::vec3& normal,
			const Texture* texture)
		{
			if (batch3DRegular.numVertices + 3 > _Batch::maxBatchSize)
			{
				g_logger_warning("Ran out of batch room for 3D stuff!");
				return;
			}

			uint32 texSlot = batch3DRegular.getTextureSlot3D(texture->graphicsId);

			// One triangle per sector
			RenderVertex3D v;
			v.position = { p0.x, p0.y, p0.z };
			v.textureSlot = texSlot;
			v.textureCoords = uv0;
			v.normal = normal;
			batch3DRegular.addVertex(v);

			v.position = { p1.x, p1.y, p1.z };
			v.textureSlot = texSlot;
			v.textureCoords = uv1;
			v.normal = normal;
			batch3DRegular.addVertex(v);

			v.position = { p2.x, p2.y, p2.z };
			v.textureSlot = texSlot;
			v.textureCoords = uv2;
			v.normal = normal;
			batch3DRegular.addVertex(v);
		}

		static Batch<RenderVertex2D>& getBatch2D(int zIndex, const Texture& texture, bool useTexture, bool isFont)
		{
			for (Batch<RenderVertex2D>& batch2D : batches2D)
			{
				if (batch2D.hasRoom() && batch2D.zIndex == zIndex &&
					(!useTexture || batch2D.hasTexture(texture.graphicsId) || batch2D.hasTextureRoom(isFont)))
				{
					return batch2D;
				}
			}

			return createBatch2D(zIndex, isFont);
		}

		static Batch<RenderVertex2D>& createBatch2D(int zIndex, bool isFont)
		{
			// No batch3DLines found, create a new one and sort the batches
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
			return getBatch2D(zIndex, {}, false, isFont);
		}
	}
}