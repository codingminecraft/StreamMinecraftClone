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
					rb.velocity.x += transform.forward.x * controller.movementAxis.x;
					rb.velocity.z += transform.forward.z * controller.movementAxis.x;
				}
				if (controller.movementAxis.z)
				{
					rb.velocity.x += transform.right.x * controller.movementAxis.z;
					rb.velocity.z += transform.right.z * controller.movementAxis.z;
				}
				rb.velocity.x *= speed;
				rb.velocity.z *= speed;

				float mx = controller.viewAxis.x;
				float my = controller.viewAxis.y;
				mx *= controller.movementSensitivity;
				my *= controller.movementSensitivity;

				transform.orientation.x += my;
				transform.orientation.y += mx;
				transform.orientation.x = glm::clamp(transform.orientation.x, -89.9f, 89.9f);
				transform.orientation.z = glm::clamp(transform.orientation.z, -89.9f, 89.9f);

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