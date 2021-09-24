#include "gameplay/CharacterSystem.h"
#include "gameplay/CharacterController.h"
#include "core/Components.h"
#include "physics/PhysicsComponents.h"

namespace Minecraft
{
	namespace CharacterSystem
	{
		static Ecs::EntityId cameraEntity = Ecs::nullEntity;

		void update(Ecs::Registry& registry, float dt)
		{
			for (Ecs::EntityId entity : registry.view<Transform, CharacterController, Rigidbody>())
			{
				Transform& transform = registry.getComponent<Transform>(entity);
				CharacterController& controller = registry.getComponent<CharacterController>(entity);
				Rigidbody& rb = registry.getComponent<Rigidbody>(entity);

				float speed = controller.controllerBaseSpeed;
				if (controller.isRunning)
				{
					speed = controller.controllerRunSpeed;
				}

				rb.velocity.x = 0;
				rb.velocity.z = 0;
				if (controller.movementAxis.x)
				{
					glm::vec2 normalDir = glm::normalize(glm::vec2(transform.forward.x, transform.forward.z));
					rb.velocity.x += normalDir.x * controller.movementAxis.x;
					rb.velocity.z += normalDir.y * controller.movementAxis.x;
				}
				if (controller.movementAxis.z)
				{
					glm::vec2 normalDir = glm::normalize(glm::vec2(transform.right.x, transform.right.z));
					rb.velocity.x += normalDir.x * controller.movementAxis.z;
					rb.velocity.z += normalDir.y * controller.movementAxis.z;
				}

				if (rb.velocity.x > 0 || rb.velocity.z > 0)
				{
					float normalDir = glm::inversesqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z);
					rb.velocity.x *= normalDir;
					rb.velocity.z *= normalDir;
					rb.velocity.x *= speed;
					rb.velocity.z *= speed;
				}

				float mx = controller.viewAxis.x;
				float my = controller.viewAxis.y;
				mx *= controller.movementSensitivity;
				my *= controller.movementSensitivity;

				transform.orientation.x += my;
				transform.orientation.y += mx;
				transform.orientation.x = glm::clamp(transform.orientation.x, -89.9f, 89.9f);
				transform.orientation.z = glm::clamp(transform.orientation.z, -89.9f, 89.9f);

				if (controller.applyJumpForce)
				{
					rb.velocity.y = controller.jumpForce;
					controller.applyJumpForce = false;
				}

				if (controller.lockedToCamera)
				{
					if (cameraEntity == Ecs::nullEntity)
					{
						cameraEntity = registry.find(TagType::Camera);
					}

					if (cameraEntity != Ecs::nullEntity && registry.hasComponent<Transform>(cameraEntity))
					{
						Transform& cameraTransform = registry.getComponent<Transform>(cameraEntity);
						cameraTransform.position = transform.position;
						cameraTransform.orientation = transform.orientation;
					}
					else
					{
						g_logger_warning("Camera is null!");
					}
				}
			}
		}
	}
}