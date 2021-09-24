#ifndef MINECRAFT_CHARACTER_CONTROLLER_H
#define MINECRAFT_CHARACTER_CONTROLLER_H
#include "core.h"

namespace Minecraft
{
	struct CharacterController
	{
		float controllerBaseSpeed;
		float controllerRunSpeed;
		float movementSensitivity;

		glm::vec3 movementAxis;
		glm::vec2 viewAxis;
		bool isRunning;
		bool lockedToCamera;
	};
}

#endif 