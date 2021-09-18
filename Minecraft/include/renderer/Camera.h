#ifndef MINECRAFT_CAMERA_H
#define MINECRAFT_CAMERA_H
#include "core.h"
#include "core/Ecs.h"

namespace Minecraft
{
	struct Camera
	{
		Ecs::EntityId cameraEntity;
		float fov;

		glm::mat4 calculateViewMatrix(Ecs::Registry& registry) const;
		glm::mat4 calculateProjectionMatrix(Ecs::Registry& registry) const;

		glm::mat4 calculateHUDViewMatrix() const;
		glm::mat4 calculateHUDProjectionMatrix() const;
	};
}

#endif