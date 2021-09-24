#ifndef MINECRAFT_CHARACTER_SYSTEM_H
#define MINECRAFT_CHARACTER_SYSTEM_H
#include "core.h"
#include "core/Ecs.h"

namespace Minecraft
{
	namespace CharacterSystem
	{
		void update(Ecs::Registry& registry, float dt);
	}
}

#endif 