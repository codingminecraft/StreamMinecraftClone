#ifndef MINECRAFT_CAMERA_H
#define MINECRAFT_CAMERA_H

#include "core.h"

namespace Minecraft
{
	struct Camera
	{
		glm::vec3 position;
		glm::vec3 orientation;
		glm::vec3 forward;
		float fov;

		//glm::mat4 projectionMatrix;
		//glm::mat4 viewMatrix;

		glm::mat4 calculateViewMatrix();
		glm::mat4 calculateProjectionMatrix() const;
	};
}

#endif