#ifndef MINECRAFT_TRANSFORM_SYSTEM_H
#define MINECRAFT_TRANSFORM_SYSTEM_H
#include "core/Ecs.h"

namespace Minecraft
{
	namespace TransformSystem
	{
		void update(float dt, Ecs::Registry& registry);
	}
}

#endif 