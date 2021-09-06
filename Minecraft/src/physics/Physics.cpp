#include "physics/Physics.h"
#include "physics/PhysicsComponents.h"
#include "core/Components.h"
#include "core/Ecs.h"
#include "world/World.h"
#include "world/BlockMap.h"

namespace Minecraft
{
	namespace Physics
	{
		static glm::vec3 gravity = glm::vec3(0.0f, 11.2f, 0.0f);
		static glm::vec3 terminalVelocity = glm::vec3(55.0f, 55.0f, 55.0f);

		static void resolveStaticCollision(Rigidbody& rb, Transform& transform, BoxCollider& boxCollider);

		void update(Ecs::Registry& registry, float dt)
		{
			for (Ecs::EntityId entity : registry.view<Transform, Rigidbody, BoxCollider>())
			{
				Rigidbody& rb = registry.getComponent<Rigidbody>(entity);
				Transform& transform = registry.getComponent<Transform>(entity);
				BoxCollider& boxCollider = registry.getComponent<BoxCollider>(entity);

				transform.position += rb.velocity * dt;
				rb.velocity += rb.acceleration * dt;
				rb.velocity -= gravity * dt;
				rb.velocity = glm::clamp(rb.velocity, -terminalVelocity, terminalVelocity);

				resolveStaticCollision(rb, transform, boxCollider);
			}
		}

		static void resolveStaticCollision(Rigidbody& rb, Transform& transform, BoxCollider& boxCollider)
		{
			glm::vec3 direction;
			direction.x = cos(glm::radians(transform.orientation.y)) * cos(glm::radians(transform.orientation.x));
			direction.y = sin(glm::radians(transform.orientation.x));
			direction.z = sin(glm::radians(transform.orientation.y)) * cos(glm::radians(transform.orientation.x));
			glm::vec3 forward = glm::normalize(direction);
			glm::vec3 localRight = glm::cross(forward, glm::vec3(0, 1, 0));
			glm::vec3 localUp = glm::cross(localRight, forward);

			glm::vec3 boxFront = transform.position + (forward * boxCollider.size.z * 0.5f);
			glm::vec3 boxBack = transform.position - (forward * boxCollider.size.z * 0.5f);
			glm::vec3 boxLeft = transform.position - (localRight * boxCollider.size.x * 0.5f);
			glm::vec3 boxRight = transform.position + (localRight * boxCollider.size.x * 0.5f);
			glm::vec3 boxBottom = transform.position - (localUp * boxCollider.size.y * 0.5f);
			glm::vec3 boxTop = transform.position + (localUp * boxCollider.size.y * 0.5f);

			BlockFormat frontBlock = BlockMap::getBlock(World::getBlock(boxFront).id);
			BlockFormat backBlock = BlockMap::getBlock(World::getBlock(boxBack).id);
			BlockFormat leftBlock = BlockMap::getBlock(World::getBlock(boxLeft).id);
			BlockFormat rightBlock = BlockMap::getBlock(World::getBlock(boxRight).id);
			BlockFormat bottomBlock = BlockMap::getBlock(World::getBlock(boxBottom).id);
			BlockFormat topBlock = BlockMap::getBlock(World::getBlock(boxTop).id);

			if (bottomBlock.isSolid)
			{
				transform.position.y = glm::ceil(boxBottom.y) + (boxCollider.size.y * 0.5f);
				if (rb.acceleration.y < 0.0f)
				{
					rb.acceleration.y = 0;
				}
			}
		}
	}
}