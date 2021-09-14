#include "physics/Physics.h"
#include "physics/PhysicsComponents.h"
#include "core/Components.h"
#include "core/Ecs.h"
#include "world/World.h"
#include "world/BlockMap.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "core/Input.h"

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
		static CollisionManifold collisionInformation(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2);
		static bool isColliding(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2);
		static float penetrationAmount(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2, const glm::vec3& axis);
		static Interval getInterval(const BoxCollider& box, const Transform& transform, const glm::vec3& axis);

		static bool singleStepPhysics = false;
		static bool stepPhysics = false;
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


			for (Ecs::EntityId entity : registry.view<Transform, Rigidbody, BoxCollider>())
			{
				Rigidbody& rb = registry.getComponent<Rigidbody>(entity);
				Transform& transform = registry.getComponent<Transform>(entity);
				BoxCollider& boxCollider = registry.getComponent<BoxCollider>(entity);

				//if (stepPhysics || !singleStepPhysics)
				//{
				transform.position += rb.velocity * dt;
				rb.velocity += rb.acceleration * dt;
				//rb.velocity -= gravity * dt;
				rb.velocity = glm::clamp(rb.velocity, -terminalVelocity, terminalVelocity);
				//}

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

				if (rb.onGround)
				{
					rb.velocity.y = 0;
					rb.acceleration.y = 0;
				}
			}

			if (Input::isKeyPressed(GLFW_KEY_C))
			{
				singleStepPhysics = false;
			}
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
						BlockFormat block = BlockMap::getBlock(World::getBlock(boxPos).id);

						BoxCollider defaultBlockCollider;
						defaultBlockCollider.size = glm::vec3(1.0f, 1.0f, 1.0f);
						Transform blockTransform;
						blockTransform.forward = glm::vec3(1, 0, 0);
						blockTransform.up = glm::vec3(0, 1, 0);
						blockTransform.right = glm::vec3(0, 0, 1);
						blockTransform.orientation = glm::vec3(0, 0, 0);
						blockTransform.scale = glm::vec3(1, 1, 1);
						blockTransform.position = boxPos;

						if (block.isSolid && isColliding(boxCollider, transform, defaultBlockCollider, blockTransform))
						{
							CollisionManifold collision = collisionInformation(boxCollider, transform, defaultBlockCollider, blockTransform);
							Style green = Styles::defaultStyle;
							green.color = "#00FF00"_hex;
							Renderer::drawLine(blockTransform.position, blockTransform.position + collision.overlap, green);

							//static bool pause = false;
							//if (!pause)
							//{
							//	pause = true;
							//	singleStepPhysics = true;
							//}

							//if (stepPhysics || !singleStepPhysics)
							//{
							transform.position += collision.overlap;
							rb.acceleration.y = 0;
							rb.velocity.y = 0;
							//rb.onGround = true;
							//}
							Renderer::drawBox(blockTransform.position, defaultBlockCollider.size, green);
						}
						else
						{
							Renderer::drawBox(blockTransform.position, defaultBlockCollider.size, Styles::defaultStyle);
						}
					}
				}
			}
		}

		static CollisionManifold collisionInformation(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2)
		{
			glm::vec3 b2ToB1 = t1.position - t2.position;
			glm::vec3 combinedHalfSize = (b1.size * 0.5f) + (b2.size * 0.5f);
			CollisionManifold res;
			res.didCollide = false;
			res.face = CollisionFace::NONE;
			res.overlap = glm::vec3();

			if (glm::abs(b2ToB1.x) < combinedHalfSize.x)
			{
				if (glm::abs(b2ToB1.y) < combinedHalfSize.y)
				{
					if (glm::abs(b2ToB1.z) < combinedHalfSize.z)
					{
						// Collision has occured, now we have to find out which face the collision
						// is happening on
						res.didCollide = true;
						glm::vec3 overlap = combinedHalfSize - glm::abs(b2ToB1);

						if (overlap.y >= overlap.x && overlap.y >= overlap.z)
						{
							// Collision is happening on the front-back faces, or the right-left faces
							if (overlap.x < overlap.z)
							{
								// Collision is happening on left-right faces
								// Figure out which face to resolve to
								if (b2ToB1.x > 0)
								{
									res.overlap = glm::vec3(overlap.x, 0, 0);
									res.face = CollisionFace::RIGHT;
								}
								else
								{
									res.overlap = glm::vec3(-overlap.x, 0, 0);
									res.face = CollisionFace::LEFT;
								}
							}
							else
							{
								// Collision is happening on front-back faces
								// Figure out which face to resolve to
								if (b2ToB1.z > 0)
								{
									res.overlap = glm::vec3(0, 0, overlap.z);
									res.face = CollisionFace::FRONT;
								}
								else
								{
									res.overlap = glm::vec3(0, 0, -overlap.z);
									res.face = CollisionFace::BACK;
								}
							}
						}
						else
						{
							// Collision is happening on top-bottom faces, figure out which one
							if (b2ToB1.y > 0)
							{
								res.overlap = glm::vec3(0, overlap.y, 0);
								res.face = CollisionFace::TOP;
							}
							else
							{
								res.overlap = glm::vec3(0, -overlap.y, 0);
								res.face = CollisionFace::BOTTOM;
							}
						}
					}
				}
			}

			return res;
		}

		static bool isColliding(const BoxCollider& b1, const Transform& t1, const BoxCollider& b2, const Transform& t2)
		{
			glm::vec3 testAxes[3];
			testAxes[0] = glm::vec3(0, 1, 0);
			testAxes[1] = glm::vec3(0, 1, 0);
			testAxes[2] = glm::vec3(0, 0, 0);

			float minPenetration = FLT_MAX;
			glm::vec3 axisOfPenetration = glm::vec3();
			for (int i = 0; i < 1; i++)
			{
				float penetration = penetrationAmount(b1, t1, b2, t2, testAxes[i]);
				if (penetration == 0)
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