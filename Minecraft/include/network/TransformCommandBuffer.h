#ifndef MINECRAFT_POSITION_COMMAND_BUFFER_H
#define MINECRAFT_POSITION_COMMAND_BUFFER_H
#include "core.h"
#include "core/Ecs.h"

namespace Minecraft
{
	struct UpdateTransformCommand
	{
		uint64 timestamp;
		Ecs::EntityId entity;
		glm::vec3 position;
		glm::vec3 orientation;
	};

	struct TransformCommandBuffer
	{
		UpdateTransformCommand* buffer;
		int size;
		int maxSize;

		void init(int maxNumPositionCommands);
		void free();

		void insert(const UpdateTransformCommand& command);
		bool predict(uint64 lagCompensation, Ecs::EntityId entity, glm::vec3* position, glm::vec3* orientation);

		inline UpdateTransformCommand& operator[](int index)
		{
#ifdef _DEBUG
			g_logger_assert(index >= 0 && index < size, "Invalid index in command buffer '%d' out of '%d'", index, size);
#endif
			return buffer[index];
		}
	};
}

#endif