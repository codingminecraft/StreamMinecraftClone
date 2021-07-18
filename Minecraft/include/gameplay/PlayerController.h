#ifndef MINECRAFT_PLAYER_CONTROLLER_H
#define MINECRAFT_PLAYER_CONTROLLER_H

namespace Minecraft
{
	// Forward declarations
	struct Camera;

	namespace PlayerController
	{
		void init(Camera* playerCamera);
		void update(float dt);
	}
}

#endif