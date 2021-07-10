#include "core.h"
#include "world/World.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "utils/TexturePacker.h"
#include "utils/ErrorCodes.h"
#include "renderer/Camera.h"
#include "world/Input.h"

static float vertices[] = {
	// first triangle
	 0.5f,  0.5f, 0.0f,      0.7f, 0.1f, 0.1f, 1.0f,     1.0f, 0.0f,// top right
	 0.5f, -0.5f, 0.0f,      0.1f, 0.7f, 0.2f, 1.0f,     1.0f, 1.0f,// bottom right
	-0.5f,  0.5f, 0.0f,      0.1f, 0.2f, 0.8f, 1.0f,     0.0f, 0.0f,// top left 
	// second triangle	     
	 0.5f, -0.5f, 0.0f,      0.1f, 0.7f, 0.2f, 1.0f,     1.0f, 1.0f,// bottom right
	-0.5f, -0.5f, 0.0f,      0.7f, 0.8f, 0.1f, 1.0f,     0.0f, 1.0f,// bottom left
	-0.5f,  0.5f, 0.0f,      0.1f, 0.2f, 0.8f, 1.0f,     0.0f, 0.0f // top left
};

namespace Minecraft
{
	namespace World
	{
		int playGame()
		{
			bool isRunning = true;
			
			glfwInit();
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

			const int windowWidth = 1920;
			const int windowHeight = 1080;
			const char* windowTitle = "OpenGL Template";
			GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, windowTitle, nullptr, nullptr);
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

			if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
			{
				return ErrorCodes::GLAD_INITIALIZATION_FAILED;
			}
			Logger::Info("GLAD initialized.");
			Logger::Info("Hello OpenGL %d.%d", GLVersion.major, GLVersion.minor);

			TexturePacker::packTextures("C:/dev/C++/MinecraftClone/assets/images/block");
			Shader shader = NShader::createShader("C:/dev/C++/MinecraftClone/assets/shaders/default.glsl");

			Texture texture;
			texture.internalFormat = ByteFormat::RGBA;
			texture.externalFormat = ByteFormat::RGBA8;
			texture.magFilter = FilterMode::Nearest;
			texture.minFilter = FilterMode::Nearest;
			TextureUtil::Generate(texture, "C:/dev/C++/MinecraftClone/assets/images/block/grass_block_side.png");

			Chunk chunk;
			ChunkRenderData data = chunk.generate();
			
			uint32 vao;
			glCreateVertexArrays(1, &vao);
			glBindVertexArray(vao);

			uint32 vbo;
			glGenBuffers(1, &vbo);

			// 2. copy our vertices array in a buffer for OpenGL to use
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, data.vertexSizeBytes, data.vertices, GL_DYNAMIC_DRAW);

			uint32 ebo;
			glGenBuffers(1, &ebo);

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(data.elementSizeBytes), data.elements, GL_DYNAMIC_DRAW);
			
			// 3. then set our vertex attributes pointers
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
			glEnableVertexAttribArray(0);

			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, uv)));
			glEnableVertexAttribArray(1);

			NShader::uploadInt(shader, "uTexture", texture.graphicsId);
			glUseProgram(shader.programId);

			Camera camera;
			camera.position = glm::vec3(0, 0, 1.0f);
			camera.fov = 45.0f;
			camera.orientation = glm::vec3(0.0f, 0.0f, 0.0f);

			glViewport(0, 0, windowWidth, windowHeight);
			while (!glfwWindowShouldClose(window) && isRunning)
			{
				glClearColor(250.0f / 255.0f, 119.0f / 255.0f, 110.0f / 255.0f, 1.0f);
				glClear(GL_COLOR_BUFFER_BIT);

				glm::mat4 projection = camera.calculateProjectionMatrix();
				glm::mat4 view = camera.calculateViewMatrix();
				NShader::uploadMat4(shader, "uProjection", projection);
				NShader::uploadMat4(shader, "uView", view);

				float sensitivity = 0.1f;
				float mx = Input::deltaMouseX;
				float my = Input::deltaMouseY;
				mx *= sensitivity;
				my *= sensitivity;

				camera.orientation.x += my;
				camera.orientation.y += mx;

				if (camera.orientation.x > 89.0f)
					camera.orientation.x = 89.0f;
				if (camera.orientation.x < -89.0f)
					camera.orientation.x = -89.0f;

				if (Input::isKeyPressed(GLFW_KEY_ESCAPE))
				{
					isRunning = false;
				}

				const float playerSpeed = 0.2f;
				if (Input::isKeyPressed(GLFW_KEY_W))
				{
					camera.position += camera.forward * playerSpeed;
				}
				else if (Input::isKeyPressed(GLFW_KEY_S))
				{
					camera.position -= camera.forward * playerSpeed;
				}

				if (Input::isKeyPressed(GLFW_KEY_A))
				{
					glm::vec3 localRight = glm::cross(camera.forward, glm::vec3(0, 1, 0));
					camera.position -= localRight * playerSpeed;
				}
				else if (Input::isKeyPressed(GLFW_KEY_D))
				{
					glm::vec3 localRight = glm::cross(camera.forward, glm::vec3(0, 1, 0));
					camera.position += localRight * playerSpeed;
				}

				glDrawArrays(GL_TRIANGLES, 0, 6);

				glfwSwapBuffers(window);

				Input::endFrame();
				glfwPollEvents();
			}

			glfwTerminate();
			return ErrorCodes::SUCCESS;
		}
	}
}