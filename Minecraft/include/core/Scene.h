#ifndef MINECRAFT_SCENE_H
#define MINECRAFT_SCENE_H
#include "core.h"
#include "core/Ecs.h"

namespace Minecraft
{
	enum class GEventType : uint8
	{
		None = 0,
		SetDeltaTime,
		PlayerKeyInput,
		PlayerMouseInput,
		SetPlayerPos,
		SetPlayerViewAxis
	};

	struct GEvent
	{
		GEventType type;
		size_t size;
		void* data;
		bool freeData;
	};

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

		void update();

		void changeScene(SceneType type);

		void reloadShaders();

		void queueMainEvent(GEventType type, void* eventData, size_t eventDataSize, bool freeData);
		void queueMainEventKey(int key, int action);
		void queueMainEventMouse(float xpos, float ypos);

		void free(bool freeGlobalResources=true);

		bool isPlayingGame();

		Ecs::Registry* getRegistry();

		Camera& getCamera();

		extern bool serializeEvents;
		extern bool playFromEventFile;
	}
}

#endif 