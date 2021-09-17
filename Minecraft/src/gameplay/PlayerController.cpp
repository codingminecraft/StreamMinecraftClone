#include "gameplay/PlayerController.h"
#include "core/Input.h"
#include "core/Ecs.h"
#include "core/Components.h"
#include "renderer/Camera.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "physics/PhysicsComponents.h"

namespace Minecraft
{
	void PlayerController::init(Ecs::EntityId inPlayerId)
	{
		playerId = inPlayerId;
		playerSpeed = 2.2f;
		movementSensitivity = 0.1f;
		runSpeed = playerSpeed * 2.0f;
	}

	void PlayerController::update(float dt, Ecs::Registry& registry)
	{
		if (registry.hasComponent<Transform>(playerId) && registry.hasComponent<Rigidbody>(playerId))
		{
			Transform& transform = registry.getComponent<Transform>(playerId);
			Rigidbody& rb = registry.getComponent<Rigidbody>(playerId);

			float mx = Input::deltaMouseX;
			float my = Input::deltaMouseY;
			mx *= movementSensitivity;
			my *= movementSensitivity;

			transform.orientation.x += my;
			transform.orientation.y += mx;
			transform.orientation.x = glm::clamp(transform.orientation.x, -89.9f, 89.9f);
			transform.orientation.z = glm::clamp(transform.orientation.z, -89.9f, 89.9f);

			float speed = playerSpeed;
			if (Input::isKeyPressed(GLFW_KEY_LEFT_CONTROL))
			{
				speed = runSpeed;
			}

			if (Input::isKeyPressed(GLFW_KEY_W))
			{
				//transform.position += transform.forward * speed;
				rb.velocity.x = transform.forward.x * speed;
				rb.velocity.z = transform.forward.z * speed;
			}
			else if (Input::isKeyPressed(GLFW_KEY_S))
			{
				//transform.position -= transform.forward * speed;
				rb.velocity.x = transform.forward.x * -speed;
				rb.velocity.z = transform.forward.z * -speed;
			}
			else if (Input::isKeyPressed(GLFW_KEY_A))
			{
				//transform.position -= transform.right * speed;
				rb.velocity.x = transform.right.x * -speed;
				rb.velocity.z = transform.right.z * -speed;
			}
			else if (Input::isKeyPressed(GLFW_KEY_D))
			{
				//transform.position += transform.right * speed;
				rb.velocity.x = transform.right.x * speed;
				rb.velocity.z = transform.right.z * speed;
			}
			else
			{
				rb.velocity.x = 0;
				rb.velocity.z = 0;
			}
		}
	}

	void RandomController::init(Ecs::EntityId inPlayerId)
	{
		playerId = inPlayerId;
		playerSpeed = 2.2f;
		movementSensitivity = 0.1f;
		runSpeed = playerSpeed * 2.0f;
	}

	void RandomController::update(float dt, Ecs::Registry& registry)
	{
		if (registry.hasComponent<Transform>(playerId) && registry.hasComponent<Rigidbody>(playerId))
		{
			Transform& transform = registry.getComponent<Transform>(playerId);
			Rigidbody& rb = registry.getComponent<Rigidbody>(playerId);

			float mx = Input::deltaMouseX;
			float my = Input::deltaMouseY;
			mx *= movementSensitivity;
			my *= movementSensitivity;

			transform.orientation.x += my;
			transform.orientation.y += mx;
			transform.orientation.x = glm::clamp(transform.orientation.x, -89.9f, 89.9f);
			transform.orientation.z = glm::clamp(transform.orientation.z, -89.9f, 89.9f);

			float speed = playerSpeed;
			if (Input::isKeyPressed(GLFW_KEY_LEFT_CONTROL))
			{
				speed = runSpeed;
			}

			if (Input::isKeyPressed(GLFW_KEY_UP))
			{
				//transform.position += transform.forward * speed;
				//transform.position += glm::vec3(1, 0, 0)* speed;
				rb.velocity.x = speed;
				
			}
			else if (Input::isKeyPressed(GLFW_KEY_DOWN))
			{
				//transform.position -= transform.forward * speed;
				//transform.position -= glm::vec3(1, 0, 0)* speed;
				rb.velocity.x = -speed;
			}
			else
			{
				rb.velocity.x = 0;
			}

			if (Input::isKeyPressed(GLFW_KEY_LEFT))
			{
				//transform.position -= transform.right * speed;
				//transform.position -= glm::vec3(0, 0, 1)* speed;
				rb.velocity.z = -speed;
			}
			else if (Input::isKeyPressed(GLFW_KEY_RIGHT))
			{
				//transform.position += transform.right * speed;
				//transform.position += glm::vec3(0, 0, 1)* speed;
				rb.velocity.z = speed;
			}
			else
			{
				rb.velocity.z = 0;
			}
		}
	}
}