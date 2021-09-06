#ifndef MINECRAFT_PHYSICS_COMPONENTS_H
#define MINECRAFT_PHYSICS_COMPONENTS_H

namespace Minecraft
{
	struct Rigidbody
	{
		glm::vec3 velocity;
		glm::vec3 acceleration;
	};

	struct BoxCollider
	{
		glm::vec3 size;
	};
}

#endif 