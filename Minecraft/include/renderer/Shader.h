#ifndef MINECRAFT_SHADER_H
#define MINECRAFT_SHADER_H
#include "core.h"

typedef unsigned int GLuint;

namespace Minecraft
{
	struct Shader
	{
		uint32 programId;
		uint32 startIndex;
		std::filesystem::path filepath;
	};

	namespace NShader
	{
		Shader createShader();
		Shader createShader(const std::filesystem::path& resourceName);

		Shader compile(const std::filesystem::path& filepath);
		void bind(const Shader& shader);
		void unbind(const Shader& shader);
		void destroy(Shader& shader);

		void uploadVec4(const Shader& shader, const char* varName, const glm::vec4& vec4);
		void uploadVec3(const Shader& shader, const char* varName, const glm::vec3& vec3);
		void uploadVec2(const Shader& shader, const char* varName, const glm::vec2& vec2);
		void uploadFloat(const Shader& shader, const char* varName, float value);
		void uploadInt(const Shader& shader, const char* varName, int value);
		void uploadIntArray(const Shader& shader, const char* varName, int size, const int* array);
		void uploadUInt(const Shader& shader, const char* varName, uint32 value);

		void uploadMat4(const Shader& shader, const char* varName, const glm::mat4& mat4);
		void uploadMat3(const Shader& shader, const char* varName, const glm::mat3& mat3);

		bool isNull(const Shader& shader);
		void clearAllShaderVariables();
	};
}

#endif
