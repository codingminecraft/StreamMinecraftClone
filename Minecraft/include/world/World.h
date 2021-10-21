#ifndef MINECRAFT_WORLD_H
#define MINECRAFT_WORLD_H
#include "core.h"
#include "core/Ecs.h"

namespace Minecraft
{
	struct Camera;

	namespace World
	{
		void init(Ecs::Registry& registry);
		void free();
		void update(float dt);
		void serialize();
		bool deserialize();

		std::string getWorldDataFilepath(const std::string& worldSavePath);

		glm::ivec2 toChunkCoords(const glm::vec3& worldCoordinates);

		const uint16 ChunkRadius = 12;
		const uint16 ChunkCapacity = (ChunkRadius * 2) * (ChunkRadius * 2);

		const uint16 ChunkWidth = 16;
		const uint16 ChunkDepth = 16;
		const uint16 ChunkHeight = 256;

		const uint16 MaxVertsPerSubChunk = 1'500;

		extern std::string savePath;
		extern std::string chunkSavePath;
		extern uint32 seed;
		extern std::atomic<float> seedAsFloat;
	}
}

#endif