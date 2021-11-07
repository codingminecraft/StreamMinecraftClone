#ifndef MINECRAFT_CUBEMAP_H
#define MINECRAFT_CUBEMAP_H
#include "core.h"
#include "renderer/Texture.h"

namespace Minecraft
{
	struct Shader;

	struct Cubemap
	{
		Texture top;
		Texture bottom;
		Texture left;
		Texture right;
		Texture front;
		Texture back;
		uint32 graphicsId;
		uint32 vao;
		uint32 vbo;

		void render(const Shader& shader, const glm::mat4& projectionMatrix, const glm::mat4& viewMatrix) const;
		void destroy();

		static Cubemap generateCubemap(const std::string& top, const std::string& bottom, const std::string& left, const std::string& right, const std::string& front, const std::string& back);
	};
}

#endif 