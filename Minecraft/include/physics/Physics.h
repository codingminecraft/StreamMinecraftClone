#ifndef MINECRAFT_PHYSICS_H
#define MINECRAFT_PHYSICS_H
#include "core.h"
#include "core/Ecs.h"

namespace Minecraft
{
	namespace Physics
	{
		void update(Ecs::Registry& registry, float dt);

		void raycast(const glm::vec3& origin, const glm::vec3& normalDirection, float maxDistance);
	}
}

#endif 