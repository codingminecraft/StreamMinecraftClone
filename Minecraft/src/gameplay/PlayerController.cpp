#include "gameplay/PlayerController.h"
#include "core/Input.h"
#include "renderer/Camera.h"

namespace Minecraft
{
	void PlayerController::init(Camera* camera)
	{
		playerCamera = camera;
		playerSpeed = 0.2f;
	}

	void PlayerController::update(float dt)
	{
		if (!playerCamera) 
		{
			g_logger_warning("Player camera pointer not set.");
			return;
		}

		float sensitivity = 0.1f;
		float mx = Input::deltaMouseX;
		float my = Input::deltaMouseY;
		mx *= sensitivity;
		my *= sensitivity;

		playerCamera->orientation.x += my;
		playerCamera->orientation.y += mx;

		if (playerCamera->orientation.x > 89.0f)
			playerCamera->orientation.x = 89.0f;
		if (playerCamera->orientation.x < -89.0f)
			playerCamera->orientation.x = -89.0f;

		float speedMultiplier = 1.0f;
		if (Input::isKeyPressed(GLFW_KEY_LEFT_CONTROL))
		{
			speedMultiplier = 2.0f;
		}

		if (Input::isKeyPressed(GLFW_KEY_W))
		{
			playerCamera->position += playerCamera->forward * playerSpeed * speedMultiplier;
		}
		else if (Input::isKeyPressed(GLFW_KEY_S))
		{
			playerCamera->position -= playerCamera->forward * playerSpeed * speedMultiplier;
		}

		if (Input::isKeyPressed(GLFW_KEY_A))
		{
			glm::vec3 localRight = glm::cross(playerCamera->forward, glm::vec3(0, 1, 0));
			playerCamera->position -= localRight * playerSpeed * speedMultiplier;
		}
		else if (Input::isKeyPressed(GLFW_KEY_D))
		{
			glm::vec3 localRight = glm::cross(playerCamera->forward, glm::vec3(0, 1, 0));
			playerCamera->position += localRight * playerSpeed * speedMultiplier;
		}
	}
}