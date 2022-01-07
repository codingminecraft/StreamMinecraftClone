#ifndef MINECRAFT_MAIN_HUD_H
#define MINECRAFT_MAIN_HUD_H
#include "core.h"
#include "core/Ecs.h"

namespace Minecraft
{
	struct Inventory;

	namespace MainHud
	{
		void init();

		void update(Inventory& playerInventory);

		void notify(const std::string& message);
		void generalMessage(Ecs::EntityId playerSpeaking, const char* message);

		void free();

		extern bool viewingCraftScreen;
		extern bool isPaused;
		extern bool hotbarVisible;
	}
}

#endif 