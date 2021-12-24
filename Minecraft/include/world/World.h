#ifndef MINECRAFT_WORLD_H
#define MINECRAFT_WORLD_H
#include "core.h"
#include "core/Ecs.h"

namespace Minecraft
{
	struct Camera;
	struct Texture;
	class Frustum;

	namespace World
	{
		void init(Ecs::Registry& registry, const char* hostname = "", int port = 0);
		void free();
		void reloadShaders();
		void regenerateWorld();
		void update(float dt, Frustum& cameraFrustum, const Texture& worldTexture);
		void serialize();
		bool deserialize();

		std::string getWorldDataFilepath(const std::string& worldSavePath);

		glm::ivec2 toChunkCoords(const glm::vec3& worldCoordinates);

		void givePlayerBlock(int blockId, int blockCount);
		bool isPlayerUnderwater();

		const uint16 ChunkRadius = 12;
		const uint16 ChunkCapacity = (uint16)((ChunkRadius * 2) * (ChunkRadius * 2) * 1.5f);

		const uint16 ChunkWidth = 16;
		const uint16 ChunkDepth = 16;
		const uint16 ChunkHeight = 256;

		const uint16 MaxVertsPerSubChunk = 2'500;

		extern std::string savePath;
		extern std::string chunkSavePath;
		extern uint32 seed;
		extern std::atomic<float> seedAsFloat;
		extern int worldTime;
		extern bool doDaylightCycle;
	}
}

#endif