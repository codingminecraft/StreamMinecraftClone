#include "gameplay/PlayerController.h"
#include "input/Input.h"
#include "core/Ecs.h"
#include "core/Components.h"
#include "renderer/Camera.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "utils/DebugStats.h"
#include "physics/PhysicsComponents.h"
#include "physics/Physics.h"
#include "gameplay/CharacterController.h"

namespace Minecraft
{
	namespace PlayerController
	{
		static Ecs::EntityId playerId = Ecs::nullEntity;
		static Style blockHighlight;

		void init()
		{
			blockHighlight = Styles::defaultStyle;
			blockHighlight.color = "#00000067"_hex;
			blockHighlight.strokeWidth = 0.02f;
		}

		void update(Ecs::Registry& registry, float dt)
		{
			if (playerId == Ecs::nullEntity || registry.getComponent<Tag>(playerId).type != TagType::Player)
			{
				playerId = registry.find(TagType::Player);
			}

			if (playerId != Ecs::nullEntity && registry.hasComponent<Transform>(playerId) && registry.hasComponent<CharacterController>(playerId)
				&& registry.hasComponent<Rigidbody>(playerId))
			{
				Transform& transform = registry.getComponent<Transform>(playerId);
				CharacterController& controller = registry.getComponent<CharacterController>(playerId);
				Rigidbody& rb = registry.getComponent<Rigidbody>(playerId);

				RaycastStaticResult res = Physics::raycastStatic(transform.position, transform.forward, 5.0f);
				if (res.hit)
				{
					Renderer::drawBox(res.blockCenter, res.blockSize, blockHighlight);
					Renderer::drawBox(res.point, glm::vec3(0.1f, 0.1f, 0.1f), Styles::defaultStyle);
				}

				controller.viewAxis.x = Input::deltaMouseX;
				controller.viewAxis.y = Input::deltaMouseY;
				controller.isRunning = Input::isKeyPressed(GLFW_KEY_LEFT_CONTROL);

				controller.movementAxis.x =
					Input::isKeyPressed(GLFW_KEY_W)
					? 1.0f
					: Input::isKeyPressed(GLFW_KEY_S)
					? -1.0f
					: 0.0f;
				controller.movementAxis.z =
					Input::isKeyPressed(GLFW_KEY_D)
					? 1.0f
					: Input::isKeyPressed(GLFW_KEY_A)
					? -1.0f
					: 0.0f;

				if (rb.onGround)
				{
					if (Input::keyBeginPress(GLFW_KEY_SPACE))
					{
						controller.applyJumpForce = true;
					}
				}
				DebugStats::playerPos = transform.position;
				DebugStats::playerOrientation = transform.orientation;

				//Physics::raycastStatic(transform.position, transform.forward, 5.0f);
			}
		}
	}
}