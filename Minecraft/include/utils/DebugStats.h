#ifndef MINECRAFT_DEBUG_STATS_H
#define MINECRAFT_DEBUG_STATS_H
#include "core.h"

namespace Minecraft
{
	namespace DebugStats
	{
		extern uint32 numDrawCalls;
		extern float lastFrameTime;
		extern glm::vec3 playerPos;
		extern glm::vec3 playerOrientation;

		void render();
	}
}

#endif 