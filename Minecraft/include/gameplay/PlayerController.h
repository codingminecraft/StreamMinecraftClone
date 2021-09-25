#ifndef MINECRAFT_PLAYER_CONTROLLER_H
#define MINECRAFT_PLAYER_CONTROLLER_H
#include "core/Ecs.h"

namespace Minecraft
{
	namespace PlayerController
	{
		void init();

		void update(Ecs::Registry& registry, float dt);
	};
}

#endif