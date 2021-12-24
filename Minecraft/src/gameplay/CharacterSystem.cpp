#include "gameplay/CharacterSystem.h"
#include "gameplay/CharacterController.h"
#include "core/Components.h"
#include "physics/PhysicsComponents.h"
#include "physics/Physics.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "utils/CMath.h"

namespace Minecraft
{
	namespace CharacterSystem
	{
		// Internal variables
		static Ecs::EntityId cameraEntity = Ecs::nullEntity;
		static glm::vec2 smoothMouse = glm::vec2();

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
				if (!rb.useGravity)
				{
					rb.velocity.y = 0;
				}
				rb.velocity.z = 0;

				float rotation = glm::radians(transform.orientation.y);
				glm::vec3 forward = glm::vec3(glm::cos(rotation), 0, glm::sin(rotation));
				glm::vec3 right = glm::vec3(-forward.z, 0, forward.x);
				if (controller.movementAxis.x)
				{
					rb.velocity.x = forward.x * controller.movementAxis.x;
					rb.velocity.z = forward.z * controller.movementAxis.x;
				}
				if (!rb.useGravity && controller.movementAxis.y)
				{
					rb.velocity.y += controller.movementAxis.y;
				}
				if (controller.movementAxis.z)
				{
					rb.velocity.x += right.x * controller.movementAxis.z;
					rb.velocity.z += right.z * controller.movementAxis.z;
				}

				if (glm::abs(rb.velocity.x) > 0 || glm::abs(rb.velocity.z) > 0 || (glm::abs(rb.velocity.y) > 0 && !rb.useGravity))
				{
					float denominator = glm::inversesqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z);
					if (!rb.useGravity && glm::abs(rb.velocity.y) > 0)
					{
						denominator = glm::inversesqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z + rb.velocity.y * rb.velocity.y);
						rb.velocity.y *= denominator * speed;
					}
					rb.velocity.x *= denominator * speed;
					rb.velocity.z *= denominator * speed;
				}

				float mx = controller.viewAxis.x;
				float my = controller.viewAxis.y;
				mx *= controller.movementSensitivity;
				my *= controller.movementSensitivity;
				smoothMouse.x = (smoothMouse.x - mx) * 0.1f;
				smoothMouse.y = (smoothMouse.y - my) * 0.1f;

				float lerp = 0.1f;
				transform.orientation.x -= smoothMouse.y;
				transform.orientation.y -= smoothMouse.x;
				transform.orientation.x = glm::clamp(transform.orientation.x, -89.9f, 89.9f);
				if (transform.orientation.y > 360.0f)
				{
					transform.orientation.y = 360.0f - transform.orientation.y;
				}
				else if (transform.orientation.y < 0)
				{
					transform.orientation.y = 360.0f + transform.orientation.y;
				}

				if (controller.applyJumpForce)
				{
					rb.velocity.y = controller.jumpForce;
					controller.applyJumpForce = false;
					controller.inMiddleOfJump = true;
				}

				// At the jump peak, we want to start falling fast
				if (controller.inMiddleOfJump && rb.velocity.y <= 0)
				{
					rb.acceleration.y = controller.downJumpForce;
					controller.inMiddleOfJump = false;
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
						cameraTransform.position = transform.position + controller.cameraOffset;
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