#ifndef MINECRAFT_MAIN_HUD_H
#define MINECRAFT_MAIN_HUD_H

namespace Minecraft
{
	namespace MainHud
	{
		void init();

		void update(float dt);

		void free();

		extern int currentInventorySlot;
	}
}

#endif 