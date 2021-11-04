#ifndef MINECRAFT_CONSTANTS_H
#define MINECRAFT_CONSTANTS_H
#include "core.h"

namespace Minecraft
{
	namespace INormals3
	{
		const glm::ivec3 Up = glm::ivec3(0, 1, 0);
		const glm::ivec3 Down = glm::ivec3(0, -1, 0);
		const glm::ivec3 Left = glm::ivec3(0, 0, -1);
		const glm::ivec3 Right = glm::ivec3(0, 0, 1);
		const glm::ivec3 Front = glm::ivec3(1, 0, 0);
		const glm::ivec3 Back = glm::ivec3(-1, 0, 0);
	}

	namespace FNormals3
	{
		const glm::vec3 Up = glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::vec3 Down = glm::vec3(0.0f, -1.0f, 0.0f);
		const glm::vec3 Left = glm::vec3(0.0f, 0.0f, -1.0f);
		const glm::vec3 Right = glm::vec3(0.0f, 0.0f, 1.0f);
		const glm::vec3 Front = glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 Back = glm::vec3(-1.0f, 0.0f, 0.0f);
	}

	namespace INormals2
	{
		const glm::ivec2 Up = glm::ivec2(1, 0);
		const glm::ivec2 Down = glm::ivec2(-1, 0);
		const glm::ivec2 Left = glm::ivec2(0, -1);
		const glm::ivec2 Right = glm::ivec2(0, 1);
	}

	namespace FNormals2
	{
		const glm::vec2 Up = glm::vec2(1.0f, 0.0f);
		const glm::vec2 Down = glm::vec2(-1.0f, 0.0f);
		const glm::vec2 Left = glm::vec2(0.0f, -1.0f);
		const glm::vec2 Right = glm::vec2(0.0f, 1.0f);
	}
}

#endif