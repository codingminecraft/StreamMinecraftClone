#include "gameplay/CharacterSystem.h"
#include "gameplay/CharacterController.h"
#include "core/Components.h"
#include "physics/PhysicsComponents.h"
#include "physics/Physics.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"

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
				if (rb.isSensor)
				{
					// TODO: Change this
					rb.velocity.y = 0;
				}
				rb.velocity.z = 0;

				float rotation = transform.orientation.y;
				glm::vec2 forward = glm::vec2(glm::cos(glm::radians(rotation)), glm::sin(glm::radians(rotation)));
				glm::vec2 right = glm::vec2(-forward.y, forward.x);
				if (controller.movementAxis.x)
				{
					rb.velocity.x += forward.x * controller.movementAxis.x;
					rb.velocity.z += forward.y * controller.movementAxis.x;
				}
				if (rb.isSensor && controller.movementAxis.y)
				{
					// TODO: Change this
					rb.velocity.y += controller.movementAxis.y * speed;
				}
				if (controller.movementAxis.z)
				{
					rb.velocity.x += right.x * controller.movementAxis.z;
					rb.velocity.z += right.y * controller.movementAxis.z;
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
				if (glm::abs(transform.orientation.y) > 360.0f)
				{
					float val = transform.orientation.y > 0 ? transform.orientation.y : -transform.orientation.y;
					transform.orientation.y = val - (360 * (int)(transform.orientation.y / 360));
				}

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