#ifndef MINECRAFT_SCENE_H
#define MINECRAFT_SCENE_H
#include "core.h"
#include "core/Ecs.h"

namespace Minecraft
{
	struct Camera;
	enum class SceneType : uint8
	{
		None,
		Game,
		MainMenu
	};

	namespace Scene
	{
		void init(SceneType type, Ecs::Registry& registry);

		void update(float dt);

		void changeScene(SceneType type);

		void free(bool freeGlobalResources=true);

		bool isPlayingGame();

		Camera& getCamera();
	}
}

#endif 