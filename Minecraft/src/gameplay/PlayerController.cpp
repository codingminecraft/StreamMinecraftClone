#include "gameplay/PlayerController.h"
#include "world/Input.h"
#include "renderer/Camera.h"

namespace Minecraft
{
	namespace PlayerController
	{
		static Camera* playerCamera = nullptr;
		static const float playerSpeed = 0.2f;

		void init(Camera* camera)
		{
			playerCamera = camera;
		}

		void update(float dt)
		{
			if (!playerCamera) 
			{
				Logger::Warning("Player camera pointer not set.");
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

			if (Input::isKeyPressed(GLFW_KEY_W))
			{
				playerCamera->position += playerCamera->forward * playerSpeed;
			}
			else if (Input::isKeyPressed(GLFW_KEY_S))
			{
				playerCamera->position -= playerCamera->forward * playerSpeed;
			}

			if (Input::isKeyPressed(GLFW_KEY_A))
			{
				glm::vec3 localRight = glm::cross(playerCamera->forward, glm::vec3(0, 1, 0));
				playerCamera->position -= localRight * playerSpeed;
			}
			else if (Input::isKeyPressed(GLFW_KEY_D))
			{
				glm::vec3 localRight = glm::cross(playerCamera->forward, glm::vec3(0, 1, 0));
				playerCamera->position += localRight * playerSpeed;
			}
		}
	}
}