#ifndef MINECRAFT_PLAYER_CONTROLLER_H
#define MINECRAFT_PLAYER_CONTROLLER_H
#include "core/Ecs.h"

namespace Minecraft
{
	// Forward declarations
	struct Camera;

	struct PlayerController
	{
		Ecs::EntityId playerId;
		float playerSpeed;
		float movementSensitivity;
		float runSpeed;

		void init(Ecs::EntityId playerId);
		void update(float dt, Ecs::Registry& registry);
	};


	struct RandomController
	{
		Ecs::EntityId playerId;
		float playerSpeed;
		float movementSensitivity;
		float runSpeed;

		void init(Ecs::EntityId controllerId);
		void update(float dt, Ecs::Registry& registry);
	};
}

#endif