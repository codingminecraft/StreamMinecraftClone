#ifndef MINECRAFT_PLAYER_CONTROLLER_H
#define MINECRAFT_PLAYER_CONTROLLER_H

namespace Minecraft
{
	// Forward declarations
	struct Camera;

	struct PlayerController
	{
		float playerSpeed;
		Camera* playerCamera;

		void init(Camera* playerCamera);
		void update(float dt);
	};
}

#endif