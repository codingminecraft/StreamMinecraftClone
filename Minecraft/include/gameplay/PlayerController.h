#ifndef MINECRAFT_PLAYER_CONTROLLER_H
#define MINECRAFT_PLAYER_CONTROLLER_H
#include "core/Ecs.h"

namespace Minecraft
{
	namespace PlayerController
	{
		// TODO: Find a better way to do this
		extern bool generateCubemap;

		void init();

		void update(Ecs::Registry& registry, float dt);
	};
}

#endif