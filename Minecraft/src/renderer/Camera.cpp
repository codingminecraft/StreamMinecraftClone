#include "renderer/Camera.h"
#include "core/Components.h"
#include "physics/PhysicsComponents.h"

namespace Minecraft
{
	glm::mat4 Camera::calculateViewMatrix(Ecs::Registry& registry) const
	{
		if (registry.hasComponent<Transform>(cameraEntity))
		{
			Transform& transform = registry.getComponent<Transform>(cameraEntity);

			return glm::lookAt(
				transform.position,
				transform.position + transform.forward,
				transform.up
			);
		}
	}

	glm::mat4 Camera::calculateProjectionMatrix(Ecs::Registry& registry) const
	{
		return glm::perspective(
			glm::radians(fov),
			1920.0f / 1080.0f,
			0.1f,
			2000.0f
		);
	};
}