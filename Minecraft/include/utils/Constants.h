#ifndef MINECRAFT_CONSTANTS_H
#define MINECRAFT_CONSTANTS_H
#include "core.h"

namespace Minecraft
{
	struct VoxelVertex;
	struct Model
	{
		const VoxelVertex* vertices;
		int verticesLength;
	};

	namespace INormals3
	{
		const glm::ivec3 Up = glm::ivec3(0, 1, 0);
		const glm::ivec3 Down = glm::ivec3(0, -1, 0);
		const glm::ivec3 Left = glm::ivec3(0, 0, -1);
		const glm::ivec3 Right = glm::ivec3(0, 0, 1);
		const glm::ivec3 Front = glm::ivec3(1, 0, 0);
		const glm::ivec3 Back = glm::ivec3(-1, 0, 0);

		const std::array<glm::ivec3, 6> CardinalDirections = {
			Up,
			Down,
			Left,
			Right,
			Front,
			Back
		};

		const std::array<glm::ivec3, 4> XZCardinalDirections = {
			Left,
			Right,
			Front,
			Back
		};
	}

	namespace FNormals3
	{
		const glm::vec3 Up = glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::vec3 Down = glm::vec3(0.0f, -1.0f, 0.0f);
		const glm::vec3 Left = glm::vec3(0.0f, 0.0f, -1.0f);
		const glm::vec3 Right = glm::vec3(0.0f, 0.0f, 1.0f);
		const glm::vec3 Front = glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 Back = glm::vec3(-1.0f, 0.0f, 0.0f);

		const std::array<glm::vec3, 6> CardinalDirections = {
			Up,
			Down,
			Left,
			Right,
			Front,
			Back
		};
	}

	namespace INormals2
	{
		const glm::ivec2 Up = glm::ivec2(1, 0);
		const glm::ivec2 Down = glm::ivec2(-1, 0);
		const glm::ivec2 Left = glm::ivec2(0, -1);
		const glm::ivec2 Right = glm::ivec2(0, 1);

		const std::array<glm::ivec2, 4> CardinalDirections = {
			Up,
			Down,
			Left,
			Right
		};
	}

	namespace FNormals2
	{
		const glm::vec2 Up = glm::vec2(1.0f, 0.0f);
		const glm::vec2 Down = glm::vec2(-1.0f, 0.0f);
		const glm::vec2 Left = glm::vec2(0.0f, -1.0f);
		const glm::vec2 Right = glm::vec2(0.0f, 1.0f);

		const std::array<glm::vec2, 4> CardinalDirections = {
			Up,
			Down,
			Left,
			Right
		};
	}

	namespace Vertices
	{
		extern uint32 fullScreenSpaceRectangleVao;

		void init();
		void free();

		Model getItemModel(const std::string& itemName);
	}

	namespace Player
	{
		const int numHotbarSlots = 9;
		const int numMainInventoryRows = 3;
		const int numMainInventoryColumns = 9;
		const int numMainInventorySlots = numMainInventoryRows * numMainInventoryColumns;
		const int numTotalSlots = numMainInventorySlots + numHotbarSlots;
	}
}

#endif