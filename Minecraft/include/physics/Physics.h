#ifndef MINECRAFT_PHYSICS_H
#define MINECRAFT_PHYSICS_H
#include "core.h"

namespace Minecraft
{
	namespace Ecs
	{
		struct Registry;
	}

	namespace Physics
	{
		void update(Ecs::Registry& registry, float dt);
	}
}

#endif 