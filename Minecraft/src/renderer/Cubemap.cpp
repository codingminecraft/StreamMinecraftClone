#include "renderer/Cubemap.h"
#include "renderer/Shader.h"

namespace Minecraft
{
	float skyboxVertices[] = {
		// positions          
		-1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		-1.0f,  1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f
	};

	void Cubemap::render(const Shader& shader, const glm::mat4& projectionMatrix, const glm::mat4& viewMatrix) const
	{
		glDepthMask(GL_FALSE);
		shader.bind();
		shader.uploadMat4("uProjection", projectionMatrix);
		shader.uploadMat4("uView", glm::mat4(glm::mat3(viewMatrix)));
		shader.uploadInt("uSkybox", 0);

		glBindVertexArray(vao);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_CUBE_MAP, graphicsId);
		glDrawArrays(GL_TRIANGLES, 0, 36);
		glDepthMask(GL_TRUE);
	}

	void Cubemap::destroy()
	{
		glDeleteBuffers(1, &vbo);
		glDeleteVertexArrays(1, &vao);
		glDeleteTextures(1, &graphicsId);
		graphicsId = UINT32_MAX;
		vbo = UINT32_MAX;
		vao = UINT32_MAX;

		top.destroy();
		bottom.destroy();
		left.destroy();
		right.destroy();
		front.destroy();
		back.destroy();
	}

	Cubemap Cubemap::generateCubemap(
		const std::string& top, 
		const std::string& bottom, 
		const std::string& left, 
		const std::string& right, 
		const std::string& front, 
		const std::string& back)
	{
		Cubemap res;
		glGenTextures(1, &res.graphicsId);
		glBindTexture(GL_TEXTURE_CUBE_MAP, res.graphicsId);

		res.top = TextureBuilder()
			.setMinFilter(FilterMode::None)
			.setMagFilter(FilterMode::None)
			.setTextureType(TextureType::_CUBEMAP_POSITIVE_Y)
			.setFilepath(top.c_str())
			.generate(true);
		res.bottom = TextureBuilder()
			.setMinFilter(FilterMode::None)
			.setMagFilter(FilterMode::None)
			.setTextureType(TextureType::_CUBEMAP_NEGATIVE_Y)
			.setFilepath(bottom.c_str())
			.generate(true);
		res.left = TextureBuilder()
			.setMinFilter(FilterMode::None)
			.setMagFilter(FilterMode::None)
			.setTextureType(TextureType::_CUBEMAP_NEGATIVE_Z)
			.setFilepath(left.c_str())
			.generate(true);
		res.right = TextureBuilder()
			.setMinFilter(FilterMode::None)
			.setMagFilter(FilterMode::None)
			.setTextureType(TextureType::_CUBEMAP_POSITIVE_Z)
			.setFilepath(right.c_str())
			.generate(true);
		res.front = TextureBuilder()
			.setMinFilter(FilterMode::None)
			.setMagFilter(FilterMode::None)
			.setTextureType(TextureType::_CUBEMAP_POSITIVE_X)
			.setFilepath(front.c_str())
			.generate(true);
		res.back = TextureBuilder()
			.setMinFilter(FilterMode::None)
			.setMagFilter(FilterMode::None)
			.setTextureType(TextureType::_CUBEMAP_NEGATIVE_X)
			.setFilepath(back.c_str())
			.generate(true);

		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		glGenVertexArrays(1, &res.vao);
		glBindVertexArray(res.vao);
		
		glGenBuffers(1, &res.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, res.vbo);

		glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0);

		return res;
	}
}