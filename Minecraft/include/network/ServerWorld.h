#ifndef MINECRAFT_SERVER_WORLD_H
#define MINECRAFT_SERVER_WORLD_H
#include "core.h"
#include "core/Ecs.h"

namespace Minecraft
{
	struct Camera;
	struct Texture;
	class Frustum;

	namespace ServerWorld
	{
		void init(Ecs::Registry& registry);
		void free();
		void update(float dt);

		void serialize();
		bool deserialize();

		void givePlayerBlock(int blockId, int blockCount, Ecs::EntityId playerId);

		std::string getWorldDataFilepath(const std::string& worldSavePath);
	}
}

#endif 