#ifndef MINECRAFT_PHYSICS_H
#define MINECRAFT_PHYSICS_H
#include "core.h"
#include "core/Ecs.h"

namespace Minecraft
{
	struct RaycastStaticResult
	{
		glm::vec3 point;
		glm::vec3 blockCenter;
		glm::vec3 blockSize;
		glm::vec3 hitNormal;
		bool hit;
	};

	namespace Physics
	{
		void init();

		void update(Ecs::Registry& registry, float dt);

		RaycastStaticResult raycastStatic(const glm::vec3& origin, const glm::vec3& normalDirection, float maxDistance, bool draw = false);

		//void raycastDynamic(Ecs::Registry& registry, const glm::vec3& origin, const glm::vec3& normalDirection, float maxDistance);
	}
}

#endif 