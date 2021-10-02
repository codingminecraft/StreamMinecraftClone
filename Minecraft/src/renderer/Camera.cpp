#include "renderer/Camera.h"
#include "core/Components.h"
#include "core/Application.h"
#include "core/Window.h"
#include "physics/PhysicsComponents.h"
#include "input/Input.h"

namespace Minecraft
{
	const glm::vec2 projectionSize = glm::vec2(6.0f, 3.0f);

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

		return glm::mat4();
	}

	glm::mat4 Camera::calculateProjectionMatrix(Ecs::Registry& registry) const
	{
		return glm::perspective(
			glm::radians(fov),
			Application::getWindow().getAspectRatio(),
			0.1f,
			2000.0f
		);
	};

	glm::mat4 Camera::calculateHUDViewMatrix() const
	{
		return glm::lookAt(
			glm::vec3(0, 0, 10),
			glm::vec3(0, 0, 9),
			glm::vec3(0, 1, 0)
		);
	}

	glm::mat4 Camera::calculateHUDProjectionMatrix() const
	{
		glm::vec2 halfSize = projectionSize / 2.0f;
		return glm::ortho(-halfSize.x, halfSize.x, -halfSize.y, halfSize.y, -0.1f, 1000.0f);
	}
}