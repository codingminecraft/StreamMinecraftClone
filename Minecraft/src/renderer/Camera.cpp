#include "renderer/Camera.h"

namespace Minecraft
{
	glm::mat4 Camera::calculateViewMatrix()
	{
		glm::vec3 direction;
		direction.x = cos(glm::radians(orientation.y)) * cos(glm::radians(orientation.x));
		direction.y = sin(glm::radians(orientation.x));
		direction.z = sin(glm::radians(orientation.y)) * cos(glm::radians(orientation.x));
		forward = glm::normalize(direction);
		// 0 1 0
		glm::vec3 localRight = glm::cross(forward, glm::vec3(0, 1, 0));
		glm::vec3 localUp = glm::cross(localRight, forward);
		
		return glm::lookAt(
			position,
			position + forward,
			localUp
		);
	}

	glm::mat4 Camera::calculateProjectionMatrix() const
	{
		return glm::perspective(
			glm::radians(fov),
			1920.0f / 1080.0f,
			0.1f,
			2000.0f
		);
	};
}