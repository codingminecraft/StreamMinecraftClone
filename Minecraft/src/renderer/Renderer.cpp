#include "renderer/Renderer.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Framebuffer.h"
#include "renderer/Styles.h"

namespace Minecraft
{
	struct Vertex
	{
		glm::vec2 position;
		glm::vec4 color;
		uint32 textureId;
		glm::vec2 textureCoords;
	};

	namespace Renderer
	{
		// Internal variables
		static uint32 vao;
		static uint32 vbo;
		static uint32 numVertices;

		static const int maxNumTrianglesPerBatch = 300;
		static const int maxNumVerticesPerBatch = maxNumTrianglesPerBatch * 3;
		static Vertex vertices[maxNumVerticesPerBatch];
		static Shader shader;
		static Shader screenShader;

		// Default screen rectangle
		static float defaultScreenQuad[] = {
			-1.0f, -1.0f,   0.0f, 0.0f, // Bottom-left
			 1.0f,  1.0f,   1.0f, 1.0f, // Top-right
			-1.0f,  1.0f,   0.0f, 1.0f, // Top-left

			-1.0f, -1.0f,   0.0f, 0.0f, // Bottom-left
			 1.0f, -1.0f,   1.0f, 0.0f, // Bottom-right
			 1.0f,  1.0f,   1.0f, 1.0f  // Top-right
		};

		static uint32 screenVao;
		static const uint32 numTextureGraphicsIds = 8;
		static uint32 numFontTextures = 0;
		static Texture textureGraphicIds[numTextureGraphicsIds];
		static int32 uTextures[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

		static uint32 getTexId(const Texture& texture)
		{
			for (int i = 0; i < numTextureGraphicsIds; i++)
			{
				if (texture.graphicsId == textureGraphicIds[i].graphicsId || i >= numFontTextures)
				{
					textureGraphicIds[i].graphicsId = texture.graphicsId;
					numFontTextures++;
					return i + 1;
				}
			}

			g_logger_warning("Could not find texture id in Renderer::drawTexture.");
			return 0;
		}

		static void setupScreenVao()
		{
			// Create the screen vao
			glCreateVertexArrays(1, &screenVao);
			glBindVertexArray(screenVao);

			uint32 screenVbo;
			glGenBuffers(1, &screenVbo);

			// Allocate space for the screen vao
			glBindBuffer(GL_ARRAY_BUFFER, screenVbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(defaultScreenQuad), defaultScreenQuad, GL_STATIC_DRAW);

			// Set up the screen vao attributes
			// The position doubles as the texture coordinates so we can use the same floats for that
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)0);
			glEnableVertexAttribArray(0);

			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)(sizeof(float) * 2));
			glEnableVertexAttribArray(1);
		}

		static void setupBatchedVao()
		{
			// Create the batched vao
			glCreateVertexArrays(1, &vao);
			glBindVertexArray(vao);

			glGenBuffers(1, &vbo);

			// Allocate space for the batched vao
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * maxNumVerticesPerBatch, NULL, GL_DYNAMIC_DRAW);

			uint32 ebo;
			glGenBuffers(1, &ebo);

			std::array<uint32, maxNumTrianglesPerBatch * 3> elements;
			for (int i = 0; i < elements.size(); i += 3)
			{
				elements[i] = i + 0;
				elements[i + 1] = i + 1;
				elements[i + 2] = i + 2;
			}
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32) * maxNumTrianglesPerBatch * 3, elements.data(), GL_DYNAMIC_DRAW);

			// Set up the batched vao attributes
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, position)));
			glEnableVertexAttribArray(0);

			glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, color)));
			glEnableVertexAttribArray(1);

			glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, sizeof(Vertex), (void*)(offsetof(Vertex, textureId)));
			glEnableVertexAttribArray(2);

			glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, textureCoords)));
			glEnableVertexAttribArray(3);
		}

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

		void init()
		{
			numVertices = 0;

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

			// Enable blending
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			// Initialize default shader
			shader.compile("assets/shaders/default.glsl");
			screenShader.compile("assets/shaders/screen.glsl");

			setupBatchedVao();
			setupScreenVao();

			for (int i = 0; i < numTextureGraphicsIds; i++)
			{
				textureGraphicIds[i] = Texture{};
			}
		}

		void render()
		{
			flushBatch();
		}

		void renderFramebuffer(const Framebuffer& framebuffer)
		{
			screenShader.bind();

			const Texture& texture = framebuffer.getColorAttachment(0);
			glActiveTexture(GL_TEXTURE0);
			texture.bind();
			screenShader.uploadInt("uTexture", 0);

			glBindVertexArray(screenVao);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}

		void drawSquare(const glm::vec2& start, const glm::vec2& size, const Style& style)
		{
			drawLine(start, start + glm::vec2{ size.x, 0 }, style);
			drawLine(start + glm::vec2{ 0, size.y }, start + size, style);
			drawLine(start, start + glm::vec2{ 0, size.y }, style);
			drawLine(start + glm::vec2{ size.x, 0 }, start + size, style);
		}

		void drawFilledSquare(const glm::vec2& start, const glm::vec2& size, const Style& style)
		{
			drawFilledTriangle(start, start + glm::vec2{ 0, size.y }, start + size, style);
			drawFilledTriangle(start, start + glm::vec2{ size.x, 0 }, start + size, style);
		}

		void drawLine(const glm::vec2& start, const glm::vec2& end, const Style& style)
		{
			// Draw the line
			glm::vec2 direction = end - start;
			glm::vec2 normalDirection = glm::normalize(direction);
			glm::vec2 perpVector = glm::normalize(glm::vec2{ normalDirection.y, -normalDirection.x });

			glm::vec2 v0 = start + (perpVector * style.strokeWidth * 0.5f);
			glm::vec2 v1 = v0 + direction;
			glm::vec2 v2 = v1 - (perpVector * style.strokeWidth);
			drawFilledTriangle(v0, v1, v2, style);
			
			glm::vec2 v3 = v0 - (perpVector * style.strokeWidth);
			glm::vec2 v4 = v3 + direction;
			drawFilledTriangle(v0, v3, v4, style);

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

				drawFilledTriangle(centerDot, bottomLeft, top, style);
				drawFilledTriangle(top, centerDot, bottomRight, style);
				drawFilledTriangle(centerDot, end + perpVector * style.strokeWidth * 0.5f, end - perpVector * style.strokeWidth * 0.5f, style);
			}
		}

		void drawFilledCircle(const glm::vec2& position, float radius, int numSegments, const Style& style)
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

				drawFilledTriangle(position, position + glm::vec2{ x, y }, position + glm::vec2{ nextX, nextY }, style);

				t += sectorSize;
			}
		}

		void drawFilledTriangle(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const Style& style)
		{
			if (numVertices + 3 >= maxNumVerticesPerBatch)
			{
				flushBatch();
			}

			// One triangle per sector
			vertices[numVertices].position = p0;
			vertices[numVertices].color = style.color;
			vertices[numVertices].textureId = 0;
			numVertices++;

			vertices[numVertices].position = p1;
			vertices[numVertices].color = style.color;
			vertices[numVertices].textureId = 0;
			numVertices++;

			vertices[numVertices].position = p2;
			vertices[numVertices].color = style.color;
			vertices[numVertices].textureId = 0;
			numVertices++;
		}

		// Internal function
		static void drawTexturedTriangle(
			const glm::vec2& p0, 
			const glm::vec2& p1, 
			const glm::vec2& p2, 
			const glm::vec2& uv0, 
			const glm::vec2& uv1, 
			const glm::vec2& uv2, 
			const Texture* texture,
			const Style& style)
		{
			if (numVertices + 3 >= maxNumVerticesPerBatch)
			{
				flushBatch();
			}

			uint32 texId = getTexId(*texture);

			// One triangle per sector
			vertices[numVertices].position = p0;
			vertices[numVertices].color = style.color;
			vertices[numVertices].textureId = texId;
			vertices[numVertices].textureCoords = uv0;
			numVertices++;

			vertices[numVertices].position = p1;
			vertices[numVertices].color = style.color;
			vertices[numVertices].textureId = texId;
			vertices[numVertices].textureCoords = uv1;
			numVertices++;

			vertices[numVertices].position = p2;
			vertices[numVertices].color = style.color;
			vertices[numVertices].textureId = texId;
			vertices[numVertices].textureCoords = uv2;
			numVertices++;
		}

		void drawTexture(const RenderableTexture& renderable, const Style& style)
		{
			if (numVertices + 6 >= maxNumVerticesPerBatch)
			{
				flushBatch();
			}

			uint32 texId = getTexId(*renderable.texture);

			drawTexturedTriangle(
				renderable.start,
				renderable.start + glm::vec2{ 0, renderable.size.y },
				renderable.start + renderable.size,
				renderable.texCoordStart,
				renderable.texCoordStart + glm::vec2{ 0, renderable.texCoordSize.y },
				renderable.texCoordStart + renderable.texCoordSize,
				renderable.texture,
				style
			);
			drawTexturedTriangle(
				renderable.start,
				renderable.start + glm::vec2{ renderable.size.x, 0 },
				renderable.start + renderable.size,
				renderable.texCoordStart,
				renderable.texCoordStart + glm::vec2{ renderable.texCoordSize.x, 0 },
				renderable.texCoordStart + renderable.texCoordSize,
				renderable.texture,
				style
			);
		}

		// TODO: Implement some font rendering
		//void drawString(const std::string& string, const Font& font, const glm::vec2& position, float scale, const glm::vec4& color)
		//{
		//	float x = position.x;
		//	float y = position.y;

		//	for (int i = 0; i < string.length(); i++)
		//	{
		//		char c = string[i];
		//		RenderableChar renderableChar = font.getCharInfo(c);
		//		float charWidth = renderableChar.texCoordSize.x * font.fontSize * scale;
		//		float charHeight = renderableChar.texCoordSize.y * font.fontSize * scale;
		//		float adjustedY = y - renderableChar.bearingY * font.fontSize * scale;

		//		drawTexture(RenderableTexture{
		//			&font.texture,
		//			{ x, adjustedY },
		//			{ charWidth, charHeight },
		//			renderableChar.texCoordStart,
		//			renderableChar.texCoordSize
		//			}, color);

		//		char nextC = i < string.length() - 1 ? string[i + 1] : '\0';
		//		//x += font.getKerning(c, nextC) * scale * font.fontSize;
		//		x += renderableChar.advance.x * scale * font.fontSize;
		//	}
		//}

		void flushBatch()
		{
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

			shader.bind();
			//shader.uploadMat4("uProjection", camera->calculateProjectionMatrix());
			//shader.uploadMat4("uView", camera->calculateViewMatrix());

			for (int i = 0; i < numTextureGraphicsIds; i++)
			{
				if (textureGraphicIds[i].graphicsId != 0)
				{
					glActiveTexture(GL_TEXTURE0 + i);
					textureGraphicIds[i].bind();
				}
			}
			shader.uploadIntArray("uFontTextures[0]", 8, uTextures);

			glBindVertexArray(vao);
			glDrawElements(GL_TRIANGLES, maxNumTrianglesPerBatch * 3, GL_UNSIGNED_INT, NULL);

			// Clear the batch
			memset(&vertices, 0, sizeof(Vertex) * maxNumVerticesPerBatch);
			numVertices = 0;
			numFontTextures = 0;
		}

		void clearColor(const glm::vec4& color)
		{
			glClearColor(color.r, color.g, color.b, color.a);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		}
	}
}