#include "core.h"
#include "core/TransformSystem.h"
#include "core/Components.h"

namespace Minecraft
{
	namespace TransformSystem
	{
		void update(Ecs::Registry& registry, float dt)
		{
			for (Ecs::EntityId entity : registry.view<Transform>())
			{
				Transform& transform = registry.getComponent<Transform>(entity);
				glm::vec3 direction;
				direction.x = cos(glm::radians(transform.orientation.y)) * cos(glm::radians(transform.orientation.x));
				direction.y = sin(glm::radians(transform.orientation.x));
				direction.z = sin(glm::radians(transform.orientation.y)) * cos(glm::radians(transform.orientation.x));
				transform.forward = glm::normalize(direction);
				transform.right = glm::cross(transform.forward, glm::vec3(0, 1, 0));
				transform.up = glm::cross(transform.right, transform.forward);
			}
		}
	}
}