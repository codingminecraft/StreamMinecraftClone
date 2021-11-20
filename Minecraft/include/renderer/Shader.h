#ifndef MINECRAFT_SHADER_H
#define MINECRAFT_SHADER_H
#include "core.h"

namespace Minecraft
{
	struct Shader
	{
		uint32 programId;
		uint32 startIndex;
		char* filepath;

		Shader();

		void compile(const char* shaderFilepath);
		void bind() const;
		void unbind() const;
		void destroy();

		void uploadVec4(const char* varName, const glm::vec4& vec4) const;
		void uploadVec3(const char* varName, const glm::vec3& vec3) const;
		void uploadVec2(const char* varName, const glm::vec2& vec2) const;
		void uploadIVec4(const char* varName, const glm::ivec4& vec4) const;
		void uploadIVec3(const char* varName, const glm::ivec3& vec3) const;
		void uploadIVec2(const char* varName, const glm::ivec2& vec2) const;
		void uploadFloat(const char* varName, float value) const;
		void uploadInt(const char* varName, int value) const;
		void uploadIntArray(const char* varName, int length, const int* array) const;
		void uploadUInt(const char* varName, uint32 value) const;
		void uploadBool(const char* varName, bool value) const;

		void uploadMat4(const char* varName, const glm::mat4& mat4) const;
		void uploadMat3(const char* varName, const glm::mat3& mat3) const;

		bool isNull() const;
	};
}

#endif
