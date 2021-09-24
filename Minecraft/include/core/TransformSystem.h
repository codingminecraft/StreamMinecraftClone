#ifndef MINECRAFT_TRANSFORM_SYSTEM_H
#define MINECRAFT_TRANSFORM_SYSTEM_H
#include "core/Ecs.h"

namespace Minecraft
{
	namespace TransformSystem
	{
		void update(Ecs::Registry& registry, float dt);
	}
}

#endif 