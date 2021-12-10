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
		SinglePlayerGame,
		LocalLanGame,
		MultiplayerGame,
		MainMenu
	};

	namespace Scene
	{
		void init(SceneType type, Ecs::Registry& registry);

		void update(float dt);

		void changeScene(SceneType type);

		void reloadShaders();

		void free(bool freeGlobalResources=true);

		bool isPlayingGame();

		Ecs::Registry* getRegistry();

		Camera& getCamera();
	}
}

#endif 