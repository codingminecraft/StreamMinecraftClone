#include "core.h"
#include "world/World.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "utils/TexturePacker.h"
#include "utils/ErrorCodes.h"
#include "renderer/Camera.h"
#include "world/Input.h"
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
		static int setupGlfw();
		static int setupGlad();
		static Texture loadWorldTexture();
		static RenderState setupRenderState(const ChunkRenderData& data, const Shader& shader, const Texture& worldTexture);
		static void render(const RenderState& renderState, const ChunkRenderData& data);

		// Settings
		static const int windowWidth = 1920;
		static const int windowHeight = 1080;
		static const glm::vec4 clearColor = glm::vec4(153.0f/255.0f, 204.0f/255.0f, 1.0f, 1.0f);
		static const char* windowTitle = "OpenGL Template";

		// Members
		static GLFWwindow* window;

		int playGame()
		{
			// Initiaize GLFW/Glad
			bool isRunning = true;

			int glfwResult = setupGlfw();
			if (glfwResult != ErrorCodes::SUCCESS)
			{
				return glfwResult;
			}

			int gladResult = setupGlad();
			if (gladResult != ErrorCodes::SUCCESS)
			{
				return gladResult;
			}

			// Initialize blocks
			//TexturePacker::packTextures("C:/dev/C++/MinecraftClone/assets/images/block", "textureFormat.yaml");
			BlockMap::loadBlocks("textureFormat.yaml", "blockFormats.yaml");
			Texture worldTexture = loadWorldTexture();
			Shader shader = NShader::createShader("C:/dev/C++/MinecraftClone/assets/shaders/default.glsl");

			// Create a chunk
			Chunk chunk;
			ChunkRenderData data = chunk.generate();
			RenderState renderState = setupRenderState(data, shader, worldTexture);

			// Setup camera
			Camera camera;
			camera.position = glm::vec3(0, 0, 1.0f);
			camera.fov = 45.0f;
			camera.orientation = glm::vec3(0.0f, 0.0f, 0.0f);
			PlayerController::init(&camera);

			// Run game loop
			while (!glfwWindowShouldClose(window) && isRunning)
			{
				glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

				glm::mat4 projection = camera.calculateProjectionMatrix();
				glm::mat4 view = camera.calculateViewMatrix();
				NShader::uploadMat4(shader, "uProjection", projection);
				NShader::uploadMat4(shader, "uView", view);

				PlayerController::update(0.0f);
				if (Input::isKeyPressed(GLFW_KEY_ESCAPE))
				{
					isRunning = false;
				}

				render(renderState, data);

				glfwSwapBuffers(window);

				Input::endFrame();
				glfwPollEvents();
			}

			// Clean up
			glfwTerminate();
			return ErrorCodes::SUCCESS;
		}

		static void render(const RenderState& renderState, const ChunkRenderData& data)
		{
			glBindVertexArray(renderState.vao);
			glDrawElements(GL_TRIANGLES, data.numElements, GL_UNSIGNED_INT, nullptr);
		}

		static int setupGlad()
		{
			if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
			{
				return ErrorCodes::GLAD_INITIALIZATION_FAILED;
			}
			Logger::Info("GLAD initialized.");
			Logger::Info("Hello OpenGL %d.%d", GLVersion.major, GLVersion.minor);

			return ErrorCodes::SUCCESS;
		}

		static int setupGlfw()
		{
			glfwInit();
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

			window = glfwCreateWindow(windowWidth, windowHeight, windowTitle, nullptr, nullptr);
			if (window == nullptr)
			{
				glfwTerminate();
				return ErrorCodes::GLFW_WINDOW_CREATION_FAILED;
			}
			glfwMakeContextCurrent(window);
			Logger::Info("GLFW window created");

			glfwSetCursorPosCallback(window, Input::mouseCallback);
			glfwSetKeyCallback(window, Input::keyCallback);
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

			return ErrorCodes::SUCCESS;
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

			// 2. Set GL state
			glEnable(GL_DEPTH_TEST);
			glEnable(GL_CULL_FACE);

			// 3. Resize viewport 
			// TODO: Hook this up to a callback
			glViewport(0, 0, windowWidth, windowHeight);

			return {
				vao,
				vbo
			};
		}
	}
}