#include "gameplay/PlayerController.h"
#include "input/Input.h"
#include "core/Ecs.h"
#include "core/Components.h"
#include "renderer/Camera.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "utils/DebugStats.h"
#include "physics/PhysicsComponents.h"
#include "gameplay/CharacterController.h"

namespace Minecraft
{
	namespace PlayerController
	{
		static Ecs::EntityId playerId = Ecs::nullEntity;

		void update(Ecs::Registry& registry, float dt)
		{
			if (playerId == Ecs::nullEntity || registry.getComponent<Tag>(playerId).type != TagType::Player)
			{
				playerId = registry.find(TagType::Player);
			}

			if (playerId != Ecs::nullEntity && registry.hasComponent<Transform>(playerId) && registry.hasComponent<CharacterController>(playerId))
			{
				Transform& transform = registry.getComponent<Transform>(playerId);
				CharacterController& controller = registry.getComponent<CharacterController>(playerId);

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
				DebugStats::playerPos = transform.position;
			}
		}
	}
}