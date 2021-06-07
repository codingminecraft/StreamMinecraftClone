#include <stdio.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cppUtils/SingleInclude.h>

#include "renderer/Shader.h"

#define GABE_CPP_UTILS_IMPL
#include <cppUtils/SingleInclude.h>
#undef GABE_CPP_UTILS_IMPL

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include "renderer/Texture.h"
#undef STB_IMAGE_IMPLEMENTATION

static float vertices[] = {
	// first triangle
	 0.5f,  0.7f, 0.0f,      0.7f, 0.1f, 0.1f, 1.0f,     1.0f, 0.0f,// top right
	 0.5f, -0.7f, 0.0f,      0.1f, 0.7f, 0.2f, 1.0f,     1.0f, 1.0f,// bottom right
	-0.5f,  0.7f, 0.0f,      0.1f, 0.2f, 0.8f, 1.0f,     0.0f, 0.0f,// top left 
	// second triangle	     
	 0.5f, -0.7f, 0.0f,      0.1f, 0.7f, 0.2f, 1.0f,     1.0f, 1.0f,// bottom right
	-0.5f, -0.7f, 0.0f,      0.7f, 0.8f, 0.1f, 1.0f,     0.0f, 1.0f,// bottom left
	-0.5f,  0.7f, 0.0f,      0.1f, 0.2f, 0.8f, 1.0f,     0.0f, 0.0f // top left
};

int main()
{
	using namespace Minecraft;
	using namespace CppUtils;

	Logger::Info("Hello OpenGL");

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
		printf("Failed to create GLFW window\n");
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		printf("Failed to initialize GLAD.\n");
		return -1;
	}

	// "C:\dev\C++\MinecraftClone\assets\images\block\grass_block_side.png";
	Shader shader = NShader::createShader("C:/dev/C++/MinecraftClone/assets/shaders/default.glsl");

	Texture texture;
	texture.InternalFormat = ByteFormat::RGBA;
	texture.ExternalFormat = ByteFormat::RGBA8;
	texture.MagFilter = FilterMode::Nearest;
	texture.MinFilter = FilterMode::Nearest;
	TextureUtil::Generate(texture, "C:/dev/C++/MinecraftClone/assets/images/block/grass_block_side.png");

	uint32 vao;
	glCreateVertexArrays(1, &vao);
	glBindVertexArray(vao);

	uint32 vbo;
	glGenBuffers(1, &vbo);

	// 2. copy our vertices array in a buffer for OpenGL to use
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	// 3. then set our vertex attributes pointers
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(sizeof(float) * 3));
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(sizeof(float) * 7));
	glEnableVertexAttribArray(2);

	NShader::uploadInt(shader, "uTexture", texture.GraphicsId);
	glUseProgram(shader.programId);
	
	glViewport(0, 0, windowWidth, windowHeight);
	while (!glfwWindowShouldClose(window))
	{
		glClearColor(250.0f / 255.0f, 119.0f / 255.0f, 110.0f / 255.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		glDrawArrays(GL_TRIANGLES, 0, 6);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}

