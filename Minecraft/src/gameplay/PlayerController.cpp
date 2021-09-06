#include "gameplay/PlayerController.h"
#include "core/Input.h"
#include "core/Ecs.h"
#include "renderer/Camera.h"
#include "core/Components.h"
#include "physics/PhysicsComponents.h"

namespace Minecraft
{
	void PlayerController::init(Ecs::EntityId inPlayerId)
	{
		playerId = inPlayerId;
		playerSpeed = 0.2f;
		movementSensitivity = 0.1f;
		runSpeed = playerSpeed * 2.0f;
	}

	void PlayerController::update(float dt, Ecs::Registry& registry)
	{
		if (registry.hasComponent<Transform>(playerId))
		{
			Transform& transform = registry.getComponent<Transform>(playerId);

			float mx = Input::deltaMouseX;
			float my = Input::deltaMouseY;
			mx *= movementSensitivity;
			my *= movementSensitivity;

			transform.orientation.x += my;
			transform.orientation.y += mx;
			transform.orientation = glm::clamp(transform.orientation, glm::vec3(-90.0f, -90.0f, 0.0f), glm::vec3(90.0f, 90.0f, 0.0f));

			float speed = playerSpeed;
			if (Input::isKeyPressed(GLFW_KEY_LEFT_CONTROL))
			{
				speed = runSpeed;
			}

			if (Input::isKeyPressed(GLFW_KEY_W))
			{
				transform.position += transform.forward * speed;
			}
			else if (Input::isKeyPressed(GLFW_KEY_S))
			{
				transform.position -= transform.forward * speed;
			}

			if (Input::isKeyPressed(GLFW_KEY_A))
			{
				transform.position -= transform.right * speed;
			}
			else if (Input::isKeyPressed(GLFW_KEY_D))
			{
				transform.position += transform.right * speed;
			}
		}
	}
}