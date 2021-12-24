#ifndef MINECRAFT_PHYSICS_COMPONENTS_H
#define MINECRAFT_PHYSICS_COMPONENTS_H

namespace Minecraft
{
	struct Rigidbody
	{
		glm::vec3 velocity;
		glm::vec3 acceleration;
		bool onGround;
		bool isSensor;
		bool useGravity;

		void zeroForces()
		{
			acceleration = glm::vec3(0.0f);
			velocity = glm::vec3(0.0f);
		}
	};

	struct BoxCollider
	{
		glm::vec3 size;
		glm::vec3 offset;
	};
}

#endif 