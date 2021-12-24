#include "physics/Physics.h"
#include "physics/PhysicsComponents.h"
#include "core/Components.h"
#include "core/Ecs.h"
#include "world/World.h"
#include "world/BlockMap.h"
#include "world/ChunkManager.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "input/Input.h"
#include "utils/CMath.h"

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
		static glm::vec3 gravity = glm::vec3(0.0f, 20.0f, 0.0f);
		static glm::vec3 terminalVelocity = glm::vec3(50.0f, 50.0f, 50.0f);
		static const float physicsUpdateRate = 1.0f / 120.0f;

		static void resolveStaticCollision(Ecs::EntityId entity, Rigidbody& rb, Transform& transform, BoxCollider& boxCollider);
		static CollisionManifold staticCollisionInformation(const Rigidbody& r1, const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2);
		static bool isColliding(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2);
		static float penetrationAmount(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2, const glm::vec3& axis);
		static Interval getInterval(const BoxCollider& box, const Transform& transform, const glm::vec3& axis);
		static void getQuadrantResult(const Transform& t1, const Transform& t2, const BoxCollider& b2Expanded, CollisionManifold* res, CollisionFace xFace, CollisionFace yFace, CollisionFace zFace);
		static glm::vec3 closestPointOnRay(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, float rayMaxDistance, const glm::vec3& point);

		void init()
		{
		}

		void update(Ecs::Registry& registry, float dt)
		{
#ifdef _USE_OPTICK
			OPTICK_EVENT();
#endif

			static float accumulatedDeltaTime = 0.0f;
			accumulatedDeltaTime += dt;

			// Never update the physics more than twice per frame. This means we will get skipped physics
			// frames, but that's probably more ideal then lagging forever
			int numUpdates = 0;
			while (accumulatedDeltaTime >= physicsUpdateRate)
			{
				accumulatedDeltaTime -= physicsUpdateRate;

				for (Ecs::EntityId entity : registry.view<Transform, Rigidbody, BoxCollider>())
				{
					Rigidbody& rb = registry.getComponent<Rigidbody>(entity);
					Transform& transform = registry.getComponent<Transform>(entity);
					BoxCollider& boxCollider = registry.getComponent<BoxCollider>(entity);

					transform.position += rb.velocity * physicsUpdateRate;
					rb.velocity += rb.acceleration * physicsUpdateRate;
					if (rb.useGravity)
					{
						rb.velocity -= gravity * physicsUpdateRate;
					}
					rb.velocity = glm::clamp(rb.velocity, -terminalVelocity, terminalVelocity);

					if (rb.isSensor)
					{
						// TODO: Change this
						continue;
					}

					resolveStaticCollision(entity, rb, transform, boxCollider);

					if (registry.getComponent<Tag>(entity).type != TagType::Player)
					{
						Style redStyle = Styles::defaultStyle;
						redStyle.color = "#FF0000"_hex;
						redStyle.strokeWidth = 0.3f;
						Renderer::drawBox(transform.position, boxCollider.size, redStyle);
					}
				}
			}
		}

		static bool doRaycast(const glm::vec3& origin, const glm::vec3& normalDirection, float maxDistance, bool draw, const glm::vec3& blockCenter, const glm::vec3& step, RaycastStaticResult* out);
		RaycastStaticResult raycastStatic(const glm::vec3& origin, const glm::vec3& normalDirection, float maxDistance, bool draw)
		{
			RaycastStaticResult result;
			result.hit = false;

			if (CMath::compare(normalDirection, glm::vec3(0, 0, 0)))
			{
				return result;
			}

			Style red = Styles::defaultStyle;
			red.color = "#E90101"_hex;
			if (draw)
			{
				Renderer::drawLine(origin, origin + normalDirection * maxDistance, red);
			}

			// NOTE: Thank God for this paper http://www.cse.yorku.ca/~amana/research/grid.pdf which outlines what I'm doing here
			glm::vec3 rayEnd = origin + normalDirection * maxDistance;
			// Do some fancy math to figure out which voxel is the next voxel
			glm::vec3 blockCenter = glm::ceil(origin);
			glm::vec3 step = glm::sign(normalDirection);
			// Max amount we can step in any direction of the ray, and remain in the voxel
			glm::vec3 blockCenterToOriginSign = glm::sign(blockCenter - origin);
			glm::vec3 goodNormalDirection = glm::vec3(
				normalDirection.x == 0.0f ? 1e-10 * blockCenterToOriginSign.x : normalDirection.x,
				normalDirection.y == 0.0f ? 1e-10 * blockCenterToOriginSign.y : normalDirection.y,
				normalDirection.z == 0.0f ? 1e-10 * blockCenterToOriginSign.z : normalDirection.z);
			glm::vec3 tDelta = ((blockCenter + step) - origin) / goodNormalDirection;
			// If any number is 0, then we max the delta so we don't get a false positive
			if (tDelta.x == 0.0f) tDelta.x = 1e10;
			if (tDelta.y == 0.0f) tDelta.y = 1e10;
			if (tDelta.z == 0.0f) tDelta.z = 1e10;
			glm::vec3 tMax = tDelta;
			float minTValue;
			do
			{
				// TODO: This shouldn't have to be calculated every step
				tDelta = (blockCenter - origin) / goodNormalDirection;
				tMax = tDelta;
				minTValue = FLT_MAX;
				if (tMax.x < tMax.y)
				{
					if (tMax.x < tMax.z)
					{
						blockCenter.x += step.x;
						// Check if we actually hit the block
						if (doRaycast(origin, normalDirection, maxDistance, draw, blockCenter, step, &result))
						{
							return result;
						}
						//tMax.x += tDelta.x;
						minTValue = tMax.x;
					}
					else
					{
						blockCenter.z += step.z;
						if (doRaycast(origin, normalDirection, maxDistance, draw, blockCenter, step, &result))
						{
							return result;
						}
						//tMax.z += tDelta.z;
						minTValue = tMax.z;
					}
				}
				else
				{
					if (tMax.y < tMax.z)
					{
						blockCenter.y += step.y;
						if (doRaycast(origin, normalDirection, maxDistance, draw, blockCenter, step, &result))
						{
							return result;
						}
						//tMax.y += tDelta.y;
						minTValue = tMax.y;
					}
					else
					{
						blockCenter.z += step.z;
						if (doRaycast(origin, normalDirection, maxDistance, draw, blockCenter, step, &result))
						{
							return result;
						}
						//tMax.z += tDelta.z;
						minTValue = tMax.z;
					}
				}
			} while (minTValue < maxDistance);

			return result;
		}

		static bool doRaycast(const glm::vec3& origin, const glm::vec3& normalDirection, float maxDistance, bool draw, const glm::vec3& blockCorner, const glm::vec3& step, RaycastStaticResult* out)
		{
			glm::vec3 blockCenter = blockCorner - (glm::vec3(0.5f) * step);
			if (draw)
			{
				Renderer::drawBox(blockCenter, glm::vec3(1.0f, 1.0f, 1.0f), Styles::defaultStyle);
			}

			int blockId = ChunkManager::getBlock(blockCenter).id;
			BlockFormat block = BlockMap::getBlock(blockId);
			if (blockId != BlockMap::NULL_BLOCK.id && blockId != BlockMap::AIR_BLOCK.id)
			{
				BoxCollider currentBox;
				currentBox.offset = glm::vec3();
				currentBox.size = glm::vec3(1.0f, 1.0f, 1.0f);
				Transform currentTransform;
				currentTransform.position = blockCenter;

				Block block = ChunkManager::getBlock(currentTransform.position);
				BlockFormat blockFormat = BlockMap::getBlock(block.id);

				if (blockFormat.isSolid)
				{
					glm::vec3 min = currentTransform.position - (currentBox.size * 0.5f) + currentBox.offset;
					glm::vec3 max = currentTransform.position + (currentBox.size * 0.5f) + currentBox.offset;
					float t1 = (min.x - origin.x) / (CMath::compare(normalDirection.x, 0.0f) ? 0.00001f : normalDirection.x);
					float t2 = (max.x - origin.x) / (CMath::compare(normalDirection.x, 0.0f) ? 0.00001f : normalDirection.x);
					float t3 = (min.y - origin.y) / (CMath::compare(normalDirection.y, 0.0f) ? 0.00001f : normalDirection.y);
					float t4 = (max.y - origin.y) / (CMath::compare(normalDirection.y, 0.0f) ? 0.00001f : normalDirection.y);
					float t5 = (min.z - origin.z) / (CMath::compare(normalDirection.z, 0.0f) ? 0.00001f : normalDirection.z);
					float t6 = (max.z - origin.z) / (CMath::compare(normalDirection.z, 0.0f) ? 0.00001f : normalDirection.z);

					float tmin = glm::max(glm::max(glm::min(t1, t2), glm::min(t3, t4)), glm::min(t5, t6));
					float tmax = glm::min(glm::min(glm::max(t1, t2), glm::max(t3, t4)), glm::max(t5, t6));
					if (tmax < 0 || tmin > tmax)
					{
						// No intersection
						return false;
					}

					float depth = 0.0f;
					if (tmin < 0.0f)
					{
						// The ray's origin is inside the AABB
						depth = tmax;
					}
					else
					{
						depth = tmin;
					}

					out->point = origin + normalDirection * depth;
					out->hit = true;
					out->blockCenter = currentTransform.position + currentBox.offset;
					out->blockSize = currentBox.size;
					out->hitNormal = out->point - out->blockCenter;
					float maxComponent = glm::max(glm::abs(out->hitNormal.x), glm::max(glm::abs(out->hitNormal.y), glm::abs(out->hitNormal.z)));
					out->hitNormal = glm::abs(out->hitNormal.x) == maxComponent
						? glm::vec3(1, 0, 0) * glm::sign(out->hitNormal.x)
						: glm::abs(out->hitNormal.y) == maxComponent
						? glm::vec3(0, 1, 0) * glm::sign(out->hitNormal.y)
						: glm::vec3(0, 0, 1) * glm::sign(out->hitNormal.z);
					return true;
				}
			}

			return false;
		}

		static void resolveStaticCollision(Ecs::EntityId entity, Rigidbody& rb, Transform& transform, BoxCollider& boxCollider)
		{
			int32 leftX = (int32)glm::ceil(transform.position.x - boxCollider.size.x * 0.5f);
			int32 rightX = (int32)glm::ceil(transform.position.x + boxCollider.size.x * 0.5f);
			int32 backZ = (int32)glm::ceil(transform.position.z - boxCollider.size.z * 0.5f);
			int32 frontZ = (int32)glm::ceil(transform.position.z + boxCollider.size.z * 0.5f);
			int32 bottomY = (int32)glm::ceil(transform.position.y - boxCollider.size.y * 0.5f);
			int32 topY = (int32)glm::ceil(transform.position.y + boxCollider.size.y * 0.5f);

			bool didCollide = false;
			for (int32 y = topY; y >= bottomY; y--)
			{
				for (int32 x = leftX; x <= rightX; x++)
				{
					for (int32 z = backZ; z <= frontZ; z++)
					{
						glm::vec3 boxPos = glm::vec3(x - 0.5f, y - 0.5f, z - 0.5f);
						Block block = ChunkManager::getBlock(boxPos);
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

						if (blockFormat.isSolid && isColliding(boxCollider, transform, defaultBlockCollider, blockTransform))
						{
							CollisionManifold collision =
								staticCollisionInformation(rb, boxCollider, transform, defaultBlockCollider, blockTransform);

							float dotProduct = glm::dot(glm::normalize(collision.overlap), glm::normalize(rb.velocity));
							if (dotProduct < 0)
							{
								// We're already moving out of the collision, don't do anything
								continue;
							}
							transform.position -= collision.overlap;
							switch (collision.face)
							{
							case CollisionFace::BOTTOM:
							case CollisionFace::TOP:
								rb.acceleration.y = 0;
								rb.velocity.y = 0;
								break;
							case CollisionFace::RIGHT:
							case CollisionFace::LEFT:
								rb.velocity.x = 0;
								rb.acceleration.x = 0;
								break;
							case CollisionFace::FRONT:
							case CollisionFace::BACK:
								rb.velocity.z = 0;
								rb.acceleration.z = 0;
								break;
							}

							// TODO: Is this hacky? Or should we have callbacks for this stuff??
							// TODO: We need callbacks somehow when we collide with blocks...
							rb.onGround = rb.onGround || collision.face == CollisionFace::BOTTOM;
							didCollide = true;
						}
					}
				}
			}

			if (!didCollide && rb.onGround && rb.velocity.y > 0)
			{
				// If we're not colliding with any object it's impossible to be on the ground
				rb.onGround = false;
			}
		}

		static CollisionManifold staticCollisionInformation(const Rigidbody& r1, const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2)
		{
			CollisionManifold res;
			res.didCollide = true;
			BoxCollider b2Expanded = b2;
			b2Expanded.size += b1.size;

			// Figure out which quadrant the collision is in and resolve it there
			glm::vec3 b1ToB2 = t1.position - t2.position;
			if (b1ToB2.x > 0 && b1ToB2.y > 0 && b1ToB2.z > 0)
			{
				// We are in the top-right-front quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::LEFT, CollisionFace::BOTTOM, CollisionFace::BACK);
			}
			else if (b1ToB2.x > 0 && b1ToB2.y > 0 && b1ToB2.z <= 0)
			{
				// We are in the top-right-back quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::LEFT, CollisionFace::BOTTOM, CollisionFace::FRONT);
			}
			else if (b1ToB2.x > 0 && b1ToB2.y <= 0 && b1ToB2.z > 0)
			{
				// We are in the bottom-right-front quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::LEFT, CollisionFace::TOP, CollisionFace::BACK);
			}
			else if (b1ToB2.x > 0 && b1ToB2.y <= 0 && b1ToB2.z <= 0)
			{
				// We are in the bottom-right-back quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::LEFT, CollisionFace::TOP, CollisionFace::FRONT);
			}
			else if (b1ToB2.x <= 0 && b1ToB2.y > 0 && b1ToB2.z > 0)
			{
				// We are in the top-left-front quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::RIGHT, CollisionFace::BOTTOM, CollisionFace::BACK);
			}
			else if (b1ToB2.x <= 0 && b1ToB2.y > 0 && b1ToB2.z <= 0)
			{
				// We are in the top-left-back quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::RIGHT, CollisionFace::BOTTOM, CollisionFace::FRONT);
			}
			else if (b1ToB2.x <= 0 && b1ToB2.y <= 0 && b1ToB2.z > 0)
			{
				// We are in the bottom-left-front quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::RIGHT, CollisionFace::TOP, CollisionFace::BACK);
			}
			else if (b1ToB2.x <= 0 && b1ToB2.y <= 0 && b1ToB2.z <= 0)
			{
				// We are in the bottom-left-back quadrant
				getQuadrantResult(t1, t2, b2Expanded, &res, CollisionFace::RIGHT, CollisionFace::TOP, CollisionFace::FRONT);
			}
			else
			{
				g_logger_error("Could not evaluate physics calculation.");
				res.didCollide = false;
			}

			return res;
		}

		static float getDirection(CollisionFace face)
		{
			switch (face)
			{
			case CollisionFace::BACK:
				return 1;
			case CollisionFace::FRONT:
				return -1;
			case CollisionFace::RIGHT:
				return -1;
			case CollisionFace::LEFT:
				return 1;
			case CollisionFace::TOP:
				return -1;
			case CollisionFace::BOTTOM:
				return 1;
			}

			g_logger_error("Could not get direction from face!");
			return 0.0001f;
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
				if (glm::abs(penetration) <= 0.001f)
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

		static glm::vec3 closestPointOnRay(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, float rayMaxDistance, const glm::vec3& point)
		{
			glm::vec3 originToPoint = point - rayOrigin;
			glm::vec3 raySegment = rayDirection * rayMaxDistance;

			float rayMagnitudeSquared = rayMaxDistance * rayMaxDistance;
			float dotProduct = glm::dot(originToPoint, raySegment);
			float distance = dotProduct / rayMagnitudeSquared; //The normalized "distance" from a to your closest point  

			if (distance < 0)     //Check if P projection is over vectorAB     
			{
				return rayOrigin;
			}
			else if (distance > 1)
			{
				return rayOrigin + (rayDirection * rayMaxDistance);
			}

			return rayOrigin + rayDirection * distance * rayMaxDistance;
		}
	}
}