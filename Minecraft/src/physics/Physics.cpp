#include "physics/Physics.h"
#include "physics/PhysicsComponents.h"
#include "core/Components.h"
#include "core/Ecs.h"
#include "world/World.h"
#include "world/BlockMap.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "input/Input.h"

namespace Minecraft
{
	struct Interval
	{
		float min;
		float max;
	};

	enum class CollisionFace : uint8
	{
		NONE = 0,
		TOP,
		BOTTOM,
		BACK,
		FRONT,
		LEFT,
		RIGHT
	};

	struct CollisionManifold
	{
		glm::vec3 overlap;
		CollisionFace face;
		bool didCollide;
	};

	namespace Physics
	{
		static glm::vec3 gravity = glm::vec3(0.0f, 11.2f, 0.0f);
		static glm::vec3 terminalVelocity = glm::vec3(55.0f, 55.0f, 55.0f);

		static void resolveStaticCollision(Rigidbody& rb, Transform& transform, BoxCollider& boxCollider);
		static CollisionManifold staticCollisionInformation(const Rigidbody& r1, const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2);
		static bool isColliding(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2);
		static float penetrationAmount(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2, const glm::vec3& axis);
		static Interval getInterval(const BoxCollider& box, const Transform& transform, const glm::vec3& axis);
		static void getQuadrantResult(const Transform& t1, const Transform& t2, const BoxCollider& b2Expanded, CollisionManifold* res, CollisionFace xFace, CollisionFace yFace, CollisionFace zFace);

		static bool singleStepPhysics = false;
		static bool stepPhysics = false;
		static bool stepMovePhysics = false;
		static float debounce = 0.0f;

		void update(Ecs::Registry& registry, float dt)
		{
			debounce -= dt;
			stepPhysics = false;
			if (singleStepPhysics && Input::isKeyPressed(GLFW_KEY_SPACE) && debounce < 0)
			{
				stepPhysics = true;
				debounce = 0.2f;
			}

			if (singleStepPhysics && Input::isKeyPressed(GLFW_KEY_Z) && debounce < 0)
			{
				stepMovePhysics = true;
				debounce = 0.2f;
			}


			for (Ecs::EntityId entity : registry.view<Transform, Rigidbody, BoxCollider>())
			{
				Rigidbody& rb = registry.getComponent<Rigidbody>(entity);
				Transform& transform = registry.getComponent<Transform>(entity);
				BoxCollider& boxCollider = registry.getComponent<BoxCollider>(entity);

				if (stepMovePhysics || !singleStepPhysics)
				{
					transform.position += rb.velocity * dt;
					rb.velocity += rb.acceleration * dt;
					rb.velocity -= gravity * dt;
					rb.velocity = glm::clamp(rb.velocity, -terminalVelocity, terminalVelocity);
				}

				Style redStyle = Styles::defaultStyle;
				redStyle.color = "#FF0000"_hex;
				redStyle.strokeWidth = 0.3f;
				Renderer::drawBox(transform.position, boxCollider.size, redStyle);
				redStyle.strokeWidth = 0.05f;
				Renderer::drawLine(transform.position, transform.position + glm::vec3(1, 0, 0), redStyle);
				redStyle.color = "#00FF00"_hex;
				Renderer::drawLine(transform.position, transform.position + glm::vec3(0, 1, 0), redStyle);
				redStyle.color = "#0000FF"_hex;
				Renderer::drawLine(transform.position, transform.position + glm::vec3(0, 0, 1), redStyle);

				resolveStaticCollision(rb, transform, boxCollider);
			}

			if (Input::isKeyPressed(GLFW_KEY_C))
			{
				singleStepPhysics = false;
			}

			stepMovePhysics = false;
		}

		static void resolveStaticCollision(Rigidbody& rb, Transform& transform, BoxCollider& boxCollider)
		{
			int32 leftX = (int32)glm::ceil(transform.position.x - boxCollider.size.x * 0.5f);
			int32 rightX = (int32)glm::ceil(transform.position.x + boxCollider.size.x * 0.5f);
			int32 backZ = (int32)glm::ceil(transform.position.z - boxCollider.size.z * 0.5f);
			int32 frontZ = (int32)glm::ceil(transform.position.z + boxCollider.size.z * 0.5f);
			int32 bottomY = (int32)glm::ceil(transform.position.y - boxCollider.size.y * 0.5f);
			int32 topY = (int32)glm::ceil(transform.position.y + boxCollider.size.y * 0.5f);

			for (int32 y = bottomY; y <= topY; y++)
			{
				for (int32 x = leftX; x <= rightX; x++)
				{
					for (int32 z = backZ; z <= frontZ; z++)
					{
						glm::vec3 boxPos = glm::vec3(x - 0.5f, y - 0.5f, z - 0.5f);
						Block block = World::getBlock(boxPos);
						BlockFormat blockFormat = BlockMap::getBlock(block.id);

						BoxCollider defaultBlockCollider;
						defaultBlockCollider.size = glm::vec3(1.0f, 1.0f, 1.0f);
						Transform blockTransform;
						blockTransform.forward = glm::vec3(1, 0, 0);
						blockTransform.up = glm::vec3(0, 1, 0);
						blockTransform.right = glm::vec3(0, 0, 1);
						blockTransform.orientation = glm::vec3(0, 0, 0);
						blockTransform.scale = glm::vec3(1, 1, 1);
						blockTransform.position = boxPos;

						//if (blockFormat.isSolid)
						//{
						//	static bool pause = false;
						//	if (!pause)
						//	{
						//		pause = true;
						//		singleStepPhysics = true;
						//	}
						//}

						if (blockFormat.isSolid && isColliding(boxCollider, transform, defaultBlockCollider, blockTransform))
						{
							CollisionManifold collision =
								staticCollisionInformation(rb, boxCollider, transform, defaultBlockCollider, blockTransform);
							Style green = Styles::defaultStyle;
							green.color = "#00FFF0"_hex;
							Renderer::drawLine(blockTransform.position, blockTransform.position + collision.overlap, green);

							if (stepPhysics || !singleStepPhysics)
							{
								transform.position -= collision.overlap;
								rb.acceleration = glm::vec3();
								rb.velocity = glm::vec3();
							}
							//Renderer::drawBox(blockTransform.position, defaultBlockCollider.size, green);
						}
						else
						{
							//Renderer::drawBox(blockTransform.position, defaultBlockCollider.size, Styles::defaultStyle);
						}
					}
				}
			}
		}

		static CollisionManifold staticCollisionInformation(const Rigidbody& r1, const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2)
		{
			CollisionManifold res;
			res.didCollide = true;
			BoxCollider b2Expanded = b2;
			b2Expanded.size += b1.size;
			//Renderer::drawBox(t2.position, b2Expanded.size, Styles::defaultStyle);
			// Figure out which quadrant the collision is in and resolve it there
			glm::vec3 b1ToB2 = t1.position - t2.position;
			if (b1ToB2.x > 0 && b1ToB2.y > 0 && b1ToB2.z > 0)
			{
				// We are in the top-right-front quadrant
				// Figure out if the collision is on the front-face top-face or right-face
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::RIGHT, CollisionFace::TOP, CollisionFace::FRONT);
			}
			else if (b1ToB2.x > 0 && b1ToB2.y > 0 && b1ToB2.z < 0)
			{
				// We are in the top-right-back quadrant
				// Figure out which face we are colliding with
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::RIGHT, CollisionFace::TOP, CollisionFace::BACK);
			}
			else if (b1ToB2.x > 0 && b1ToB2.y < 0 && b1ToB2.z > 0)
			{
				// We are in the bottom-right-front quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::RIGHT, CollisionFace::BOTTOM, CollisionFace::FRONT);
			}
			else if (b1ToB2.x > 0 && b1ToB2.y < 0 && b1ToB2.z < 0)
			{
				// We are in the bottom-right-back quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::RIGHT, CollisionFace::BOTTOM, CollisionFace::BACK);
			}
			else if (b1ToB2.x < 0 && b1ToB2.y > 0 && b1ToB2.z > 0)
			{
				// We are in the top-left-front quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::LEFT, CollisionFace::TOP, CollisionFace::FRONT);
			}
			else if (b1ToB2.x < 0 && b1ToB2.y > 0 && b1ToB2.z < 0)
			{
				// We are in the top-left-back quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::LEFT, CollisionFace::TOP, CollisionFace::BACK);
			}
			else if (b1ToB2.x < 0 && b1ToB2.y < 0 && b1ToB2.z > 0)
			{
				// We are in the bottom-left-front quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::LEFT, CollisionFace::BOTTOM, CollisionFace::FRONT);
			}
			else if (b1ToB2.x < 0 && b1ToB2.y < 0 && b1ToB2.z < 0)
			{
				// We are in the bottom-left-back quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::LEFT, CollisionFace::BOTTOM, CollisionFace::BACK);
			}

			return res;
		}

		static float getDirection(CollisionFace face)
		{
			switch (face)
			{
			case CollisionFace::BACK:
				return -1;
			case CollisionFace::FRONT:
				return 1;
			case CollisionFace::RIGHT:
				return 1;
			case CollisionFace::LEFT:
				return -1;
			case CollisionFace::TOP:
				return 1;
			case CollisionFace::BOTTOM:
				return -1;
			}
			return 1;
		}

		static void getQuadrantResult(const Transform& t1, const Transform& t2, const BoxCollider& b2Expanded, CollisionManifold* res,
			CollisionFace xFace, CollisionFace yFace, CollisionFace zFace)
		{
			float xDirection = getDirection(xFace);
			float yDirection = getDirection(yFace);
			float zDirection = getDirection(zFace);

			glm::vec3 b2ExpandedSizeByDirection = {
				b2Expanded.size.x * xDirection,
				b2Expanded.size.y * yDirection,
				b2Expanded.size.z * zDirection
			};
			glm::vec3 quadrant = (b2ExpandedSizeByDirection * 0.5f) + t2.position;
			//Style redStyle = Styles::defaultStyle;
			//redStyle.color = "#FB0203"_hex;
			//Renderer::drawBox(t2.position + b2ExpandedSizeByDirection * 0.25f, b2ExpandedSizeByDirection * 0.5f, redStyle);
			glm::vec3 delta = t1.position - quadrant;
			glm::vec3 absDelta = glm::abs(delta);

			if (absDelta.x < absDelta.y && absDelta.x < absDelta.z)
			{
				// We are colliding with the right-face
				res->overlap = delta.x * glm::vec3(1, 0, 0);
				res->face = xFace;
			}
			else if (absDelta.y < absDelta.x && absDelta.y < absDelta.z)
			{
				// We are colliding with the top-face
				res->overlap = delta.y * glm::vec3(0, 1, 0);
				res->face = yFace;
			}
			else
			{
				// We are colliding with the front-face
				res->overlap = delta.z * glm::vec3(0, 0, 1);
				res->face = zFace;
			}
		}

		static bool isColliding(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2)
		{
			glm::vec3 testAxes[3];
			testAxes[0] = glm::vec3(1, 0, 0);
			testAxes[1] = glm::vec3(0, 1, 0);
			testAxes[2] = glm::vec3(0, 0, 1);

			float minPenetration = FLT_MAX;
			glm::vec3 axisOfPenetration = glm::vec3();
			for (int i = 0; i < 3; i++)
			{
				float penetration = penetrationAmount(b1, t1, b2, t2, testAxes[i]);
				if (glm::abs(penetration) <= 0.01f)
				{
					return false;
				}
				else if (glm::abs(penetration) < minPenetration)
				{
					minPenetration = glm::abs(penetration);
					axisOfPenetration = penetration > 0 ? testAxes[i] : -testAxes[i];
				}
			}

			return true;
		}

		static float penetrationAmount(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2, const glm::vec3& axis)
		{
			//Interval a = getInterval(b1, t1, axis);
			//Interval b = getInterval(b2, t2, axis);
			glm::vec3 min1 = t1.position - (b1.size * 0.5f);
			glm::vec3 max1 = t1.position + (b1.size * 0.5f);
			glm::vec3 min2 = t2.position - (b2.size * 0.5f);
			glm::vec3 max2 = t2.position + (b2.size * 0.5f);

			if (axis == glm::vec3(1, 0, 0))
			{
				if ((min2.x <= max1.x) && (min1.x <= max2.x))
				{
					// We have penetration
					return min2.x - max1.x;
				}
			}
			else if (axis == glm::vec3(0, 1, 0))
			{
				if ((min2.y <= max1.y) && (min1.y <= max2.y))
				{
					// We have penetration
					return max2.y - min1.y;
				}
			}
			else if (axis == glm::vec3(0, 0, 1))
			{
				if ((min2.z <= max1.z) && (min1.z <= max2.z))
				{
					// We have penetration
					return min2.z - max1.z;
				}
			}

			return 0.0f;
		}

		static Interval getInterval(const BoxCollider& box, const Transform& transform, const glm::vec3& axis)
		{
			glm::vec3 vertices[8];
			glm::vec3 center = transform.position;
			glm::vec3 halfSize = box.size * 0.5f;
			vertices[0] = center + glm::vec3(0, 0, 1) * halfSize.z + glm::vec3(0, 1, 0) * halfSize.y + glm::vec3(1, 0, 0) * halfSize.x; // ForwardTopRight
			vertices[1] = center + glm::vec3(0, 0, 1) * halfSize.z + glm::vec3(0, 1, 0) * halfSize.y - glm::vec3(1, 0, 0) * halfSize.x; // ForwardTopLeft
			vertices[2] = center + glm::vec3(0, 0, 1) * halfSize.z - glm::vec3(0, 1, 0) * halfSize.y + glm::vec3(1, 0, 0) * halfSize.x; // ForwardBottomRight
			vertices[3] = center + glm::vec3(0, 0, 1) * halfSize.z - glm::vec3(0, 1, 0) * halfSize.y - glm::vec3(1, 0, 0) * halfSize.x; // ForwardBottomLeft
			vertices[4] = center - glm::vec3(0, 0, 1) * halfSize.z + glm::vec3(0, 1, 0) * halfSize.y + glm::vec3(1, 0, 0) * halfSize.x; // BackTopRight
			vertices[5] = center - glm::vec3(0, 0, 1) * halfSize.z + glm::vec3(0, 1, 0) * halfSize.y - glm::vec3(1, 0, 0) * halfSize.x; // BackTopLeft
			vertices[6] = center - glm::vec3(0, 0, 1) * halfSize.z - glm::vec3(0, 1, 0) * halfSize.y + glm::vec3(1, 0, 0) * halfSize.x; // BackBottomRight
			vertices[7] = center - glm::vec3(0, 0, 1) * halfSize.z - glm::vec3(0, 1, 0) * halfSize.y - glm::vec3(1, 0, 0) * halfSize.x; // BackBottomLeft

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