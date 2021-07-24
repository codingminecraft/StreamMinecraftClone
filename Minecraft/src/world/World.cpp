#include "core.h"
#include "world/World.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Camera.h"
#include "utils/TexturePacker.h"
#include "core/Input.h"
#include "core/Application.h"
#include "world/BlockMap.h"
#include "gameplay/PlayerController.h"

namespace Minecraft
{
	struct RenderState
	{
		uint32 vao;
		uint32 vbo;
	};

	namespace World
	{
		// Internal declarations
		static Texture loadWorldTexture();
		static RenderState setupRenderState(const ChunkRenderData& data, const Shader& shader, const Texture& worldTexture);
		static void render(const RenderState& renderState, const ChunkRenderData& data);

		// Members
		static GLFWwindow* window;
		static Chunk chunk;
		static ChunkRenderData data;
		static RenderState renderState;
		static Camera camera;
		static Shader shader;
		static Texture worldTexture;

		void init()
		{
			// Initialize blocks
			//TexturePacker::packTextures("C:/dev/C++/MinecraftClone/assets/images/block", "textureFormat.yaml");
			BlockMap::loadBlocks("textureFormat.yaml", "blockFormats.yaml");
			worldTexture = loadWorldTexture();
			shader = NShader::createShader("C:/dev/C++/MinecraftClone/assets/shaders/default.glsl");

			// Create a chunk
			data = chunk.generate();
			renderState = setupRenderState(data, shader, worldTexture);

			// Setup camera
			camera.position = glm::vec3(0, 100.0f, 1.0f);
			camera.fov = 45.0f;
			camera.orientation = glm::vec3(0.0f, 0.0f, 0.0f);
			PlayerController::init(&camera);
		}

		void update(float dt)
		{
			glm::mat4 projection = camera.calculateProjectionMatrix();
			glm::mat4 view = camera.calculateViewMatrix();
			NShader::uploadMat4(shader, "uProjection", projection);
			NShader::uploadMat4(shader, "uView", view);

			PlayerController::update(0.0f);

			if (Input::isKeyPressed(GLFW_KEY_F1))
			{
				Application::lockCursor(false);
			} 
			else if (Input::isKeyPressed(GLFW_KEY_F2))
			{
				Application::lockCursor(true);
			}

			render(renderState, data);
		}

		static void render(const RenderState& renderState, const ChunkRenderData& data)
		{
			glBindVertexArray(renderState.vao);
			glDrawElements(GL_TRIANGLES, data.numElements, GL_UNSIGNED_INT, nullptr);
		}

		static Texture loadWorldTexture()
		{
			Texture texture;
			texture.internalFormat = ByteFormat::RGBA;
			texture.externalFormat = ByteFormat::RGBA8;
			texture.magFilter = FilterMode::Nearest;
			texture.minFilter = FilterMode::Nearest;
			TextureUtil::Generate(texture, "C:/dev/C++/MinecraftClone/test.png");

			return texture;
		}

		static RenderState setupRenderState(const ChunkRenderData& data, const Shader& shader, const Texture& worldTexture)
		{
			// 1. Buffer the data
			uint32 vao;
			glCreateVertexArrays(1, &vao);
			glBindVertexArray(vao);

			uint32 vbo;
			glGenBuffers(1, &vbo);

			// 1a. copy our vertices array in a buffer for OpenGL to use
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, data.vertexSizeBytes, data.vertices, GL_STATIC_DRAW);

			uint32 ebo;
			glGenBuffers(1, &ebo);

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.elementSizeBytes, data.elements, GL_STATIC_DRAW);

			// 1b. then set our vertex attributes pointers
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
			glEnableVertexAttribArray(0);

			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, uv)));
			glEnableVertexAttribArray(1);

			NShader::uploadInt(shader, "uTexture", worldTexture.graphicsId);
			glUseProgram(shader.programId);

			return {
				vao,
				vbo
			};
		}

	}
}