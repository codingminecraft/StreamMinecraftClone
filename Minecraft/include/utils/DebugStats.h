#ifndef MINECRAFT_DEBUG_STATS_H
#define MINECRAFT_DEBUG_STATS_H
#include "core.h"
#include "world/BlockMap.h"

namespace Minecraft
{
	namespace DebugStats
	{
		extern uint32 numDrawCalls;
		extern float lastFrameTime;
		extern glm::vec3 playerPos;
		extern glm::vec3 playerOrientation;
		extern std::atomic<float> totalChunkRamUsed;
		extern float totalChunkRamAvailable;
		extern Block blockLookingAt;
		extern Block airBlockLookingAt;

		void render();
	}
}

#endif 