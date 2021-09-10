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

	struct CollisionManifold
	{
		float penetrationAmount;
		glm::vec3 axisOfPenetration;
		bool didCollide;
	};

	namespace Physics
	{
		static glm::vec3 gravity = glm::vec3(0.0f, 11.2f, 0.0f);
		static glm::vec3 terminalVelocity = glm::vec3(55.0f, 55.0f, 55.0f);

		static void resolveStaticCollision(Rigidbody& rb, Transform& transform, BoxCollider& boxCollider);
		static CollisionManifold boxVsBox(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2);
		static float penetrationAmount(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2, const glm::vec3& axis);
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
				rb.velocity -= gravity * dt;
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
			glm::vec3 boxFront = transform.position + (transform.forward * boxCollider.size.z * 0.5f);
			glm::vec3 boxBack = transform.position - (transform.forward * boxCollider.size.z * 0.5f);
			glm::vec3 boxLeft = transform.position - (transform.right * boxCollider.size.x * 0.5f);
			glm::vec3 boxRight = transform.position + (transform.right * boxCollider.size.x * 0.5f);
			glm::vec3 boxBottom = transform.position - (transform.up * boxCollider.size.y * 0.5f);
			glm::vec3 boxTop = transform.position + (transform.up * boxCollider.size.y * 0.5f);

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
			CollisionManifold collision = boxVsBox(boxCollider, transform, defaultBlockCollider, blockTransform);
			if (bottomBlock.isSolid && collision.didCollide)
			{
				transform.position -= collision.axisOfPenetration * collision.penetrationAmount;
				rb.acceleration.y = 0;
				rb.velocity.y = 0;
				rb.onGround = true;
			}
			else
			{
				rb.onGround = false;
			}

			blockTransform.position = glm::ceil(boxFront) - glm::vec3(0.5f, 0.5f, 0.5f);
			collision = boxVsBox(boxCollider, transform, defaultBlockCollider, blockTransform);
			if (frontBlock.isSolid && collision.didCollide)
			{
				transform.position -= collision.axisOfPenetration * collision.penetrationAmount;
				rb.acceleration.z = 0;
				rb.velocity.z = 0;
			}

			blockTransform.position = glm::ceil(boxBack) - glm::vec3(0.5f, 0.5f, 0.5f);
			collision = boxVsBox(boxCollider, transform, defaultBlockCollider, blockTransform);
			if (backBlock.isSolid && collision.didCollide)
			{
				transform.position -= collision.axisOfPenetration * collision.penetrationAmount;
				rb.acceleration.z = 0;
				rb.velocity.z = 0;
			}

			blockTransform.position = glm::ceil(boxLeft) - glm::vec3(0.5f, 0.5f, 0.5f);
			collision = boxVsBox(boxCollider, transform, defaultBlockCollider, blockTransform);
			if (leftBlock.isSolid && collision.didCollide)
			{
				transform.position -= collision.axisOfPenetration * collision.penetrationAmount;
				rb.acceleration.x = 0;
				rb.velocity.x = 0;
			}

			blockTransform.position = glm::ceil(boxRight) - glm::vec3(0.5f, 0.5f, 0.5f);
			collision = boxVsBox(boxCollider, transform, defaultBlockCollider, blockTransform);
			if (rightBlock.isSolid && collision.didCollide)
			{
				transform.position -= collision.axisOfPenetration * collision.penetrationAmount;
				rb.acceleration.x = 0;
				rb.velocity.x = 0;
			}
		}

		static CollisionManifold boxVsBox(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2)
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

			float minPenetration = FLT_MAX;
			glm::vec3 axisOfPenetration = glm::vec3();
			for (int i = 0; i < 15; i++)
			{
				float penetration = penetrationAmount(b1, t1, b2, t2, testAxes[i]);
				if (penetration == 0)
				{
					// Separating axis found
					CollisionManifold res;
					res.axisOfPenetration = glm::vec3();
					res.penetrationAmount = 0.0f;
					res.didCollide = false;
					return res;
				}
				else if (glm::abs(penetration) < minPenetration)
				{
					minPenetration = glm::abs(penetration);
					axisOfPenetration = penetration > 0 ? testAxes[i] : -testAxes[i];
				}
			}

			// No separating axis found
			CollisionManifold res;
			res.axisOfPenetration = axisOfPenetration;
			res.penetrationAmount = minPenetration;
			res.didCollide = true;
			return res;
		}

		static float penetrationAmount(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2, const glm::vec3& axis)
		{
			Interval a = getInterval(b1, t1, axis);
			Interval b = getInterval(b2, t2, axis);
			if ((b.min <= a.max) && (a.min <= b.max))
			{
				// We have penetration
				return b.min - a.max;
			}
			return 0.0f;
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