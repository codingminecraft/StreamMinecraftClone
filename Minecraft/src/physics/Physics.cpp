#include "physics/Physics.h"
#include "physics/PhysicsComponents.h"
#include "core/Components.h"
#include "core/Ecs.h"
#include "world/World.h"
#include "world/BlockMap.h"

namespace Minecraft
{
	struct Interval
	{
		float min;
		float max;
	};

	namespace Physics
	{
		static glm::vec3 gravity = glm::vec3(0.0f, 11.2f, 0.0f);
		static glm::vec3 terminalVelocity = glm::vec3(55.0f, 55.0f, 55.0f);

		static void resolveStaticCollision(Rigidbody& rb, Transform& transform, BoxCollider& boxCollider);
		static bool boxVsBox(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2);
		static bool overlapOnAxis(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2, const glm::vec3& axis);
		static Interval getInterval(const BoxCollider& box, const Transform& transform, const glm::vec3& axis);

		void update(Ecs::Registry& registry, float dt)
		{
			for (Ecs::EntityId entity : registry.view<Transform, Rigidbody, BoxCollider>())
			{
				Rigidbody& rb = registry.getComponent<Rigidbody>(entity);
				Transform& transform = registry.getComponent<Transform>(entity);
				BoxCollider& boxCollider = registry.getComponent<BoxCollider>(entity);

				transform.position += rb.velocity * dt;
				rb.velocity += rb.acceleration * dt;
				//rb.velocity -= gravity * dt;
				rb.velocity = glm::clamp(rb.velocity, -terminalVelocity, terminalVelocity);

				resolveStaticCollision(rb, transform, boxCollider);

				if (rb.onGround)
				{
					rb.velocity.y = 0;
					rb.acceleration.y = 0;
				}
			}
		}

		static void resolveStaticCollision(Rigidbody& rb, Transform& transform, BoxCollider& boxCollider)
		{
			glm::vec3 forward = glm::vec3(0, 0, 1);
			glm::vec3 localRight = glm::vec3(1, 0, 0);
			glm::vec3 localUp = glm::vec3(0, 1, 0);

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

			BoxCollider defaultBlockCollider;
			defaultBlockCollider.size = glm::vec3(1.0f, 1.0f, 1.0f);
			Transform blockTransform;
			blockTransform.forward = glm::vec3(1, 0, 0);
			blockTransform.up = glm::vec3(0, 1, 0);
			blockTransform.right = glm::vec3(0, 0, 1);
			blockTransform.orientation = glm::vec3(0, 0, 0);
			blockTransform.scale = glm::vec3(1, 1, 1);

			blockTransform.position = glm::ceil(boxBottom) - glm::vec3(0.5f, 0.5f, 0.5f);
			if (bottomBlock.isSolid && boxVsBox(boxCollider, transform, defaultBlockCollider, blockTransform))
			{
				transform.position.y = glm::ceil(boxBottom.y) + (boxCollider.size.y * 0.5f);
				rb.acceleration.y = 0;
				rb.velocity.y = 0;
				rb.onGround = true;
			}
			else
			{
				rb.onGround = false;
			}

			blockTransform.position = glm::ceil(boxFront) - glm::vec3(0.5f, 0.5f, 0.5f);
			if (frontBlock.isSolid && boxVsBox(boxCollider, transform, defaultBlockCollider, blockTransform))
			{
				transform.position.z = glm::floor(boxFront.z) - (boxCollider.size.z * 0.5f);
				rb.acceleration.z = 0;
				rb.velocity.z = 0;
			}

			blockTransform.position = glm::ceil(boxBack) - glm::vec3(0.5f, 0.5f, 0.5f);
			if (backBlock.isSolid && boxVsBox(boxCollider, transform, defaultBlockCollider, blockTransform))
			{
				transform.position.z = glm::ceil(boxBack.z) + (boxCollider.size.z * 0.5f);
				rb.acceleration.z = 0;
				rb.velocity.z = 0;
			}

			blockTransform.position = glm::ceil(boxLeft) - glm::vec3(0.5f, 0.5f, 0.5f);
			if (leftBlock.isSolid && boxVsBox(boxCollider, transform, defaultBlockCollider, blockTransform))
			{
				transform.position.x = glm::ceil(boxLeft.x) + (boxCollider.size.x * 0.5f);
				rb.acceleration.x = 0;
				rb.velocity.x = 0;
			}

			blockTransform.position = glm::ceil(boxRight) - glm::vec3(0.5f, 0.5f, 0.5f);
			if (rightBlock.isSolid && boxVsBox(boxCollider, transform, defaultBlockCollider, blockTransform))
			{
				transform.position.x = glm::floor(boxRight.x) - (boxCollider.size.x * 0.5f);
				rb.acceleration.x = 0;
				rb.velocity.x = 0;
			}
		}

		static bool boxVsBox(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2)
		{
			glm::vec3 testAxes[15];
			testAxes[0] = t1.forward;
			testAxes[1] = t1.right;
			testAxes[2] = t1.up;
			testAxes[3] = t2.forward;
			testAxes[4] = t2.right;
			testAxes[5] = t2.up;

			// Fill the rest of the axes with the cross products of all axes of the two boxes
			for (int i = 0; i < 3; i++)
			{
				// TODO: He had these different, this might be wrong
				// TODO: Page 212 at the bottom
				testAxes[6 + i * 3 + 0] = glm::cross(testAxes[i], testAxes[3]);
				testAxes[6 + i * 3 + 1] = glm::cross(testAxes[i], testAxes[4]);
				testAxes[6 + i * 3 + 2] = glm::cross(testAxes[i], testAxes[5]);
			}

			for (int i = 0; i < 15; i++)
			{
				if (!overlapOnAxis(b1, t1, b2, t2, testAxes[i]))
				{
					// Separating axis found
					return false;
				}
			}

			// No separating axis found
			return true;
		}

		static bool overlapOnAxis(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2, const glm::vec3& axis)
		{
			Interval a = getInterval(b1, t1, axis);
			Interval b = getInterval(b2, t2, axis);
			return ((b.min <= a.max) && (a.min <= b.max));
		}

		static Interval getInterval(const BoxCollider& box, const Transform& transform, const glm::vec3& axis)
		{
			glm::vec3 vertices[8];
			glm::vec3 center = transform.position;
			glm::vec3 halfSize = box.size * 0.5f;
			vertices[0] = center + transform.forward * halfSize.z + transform.up * halfSize.y + transform.right * halfSize.x; // ForwardTopRight
			vertices[1] = center + transform.forward * halfSize.z + transform.up * halfSize.y - transform.right * halfSize.x; // ForwardTopLeft
			vertices[2] = center + transform.forward * halfSize.z - transform.up * halfSize.y + transform.right * halfSize.x; // ForwardBottomRight
			vertices[3] = center + transform.forward * halfSize.z - transform.up * halfSize.y - transform.right * halfSize.x; // ForwardBottomLeft
			vertices[4] = center - transform.forward * halfSize.z + transform.up * halfSize.y + transform.right * halfSize.x; // BackTopRight
			vertices[5] = center - transform.forward * halfSize.z + transform.up * halfSize.y - transform.right * halfSize.x; // BackTopLeft
			vertices[6] = center - transform.forward * halfSize.z - transform.up * halfSize.y + transform.right * halfSize.x; // BackBottomRight
			vertices[7] = center - transform.forward * halfSize.z - transform.up * halfSize.y - transform.right * halfSize.x; // BackBottomLeft

			Interval result;
			result.min = result.max = glm::dot(axis, vertices[0]);
			for (int i = 1; i < 8; i++)
			{
				float projection = glm::dot(axis, vertices[i]);
				result.min = glm::min(result.min, projection);
				result.max = glm::max(result.max, projection);
			}

			return result;
		}
	}
}