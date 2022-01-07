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
		void init(Ecs::Registry& registry, bool isLanClient = false);
		void free(bool shouldSerialize = true);
		void reloadShaders();
		void regenerateWorld();
		void update(Frustum& cameraFrustum, const Texture& worldTexture);
		void serialize();
		bool deserialize();

		std::string getWorldDataFilepath(const std::string& worldSavePath);
		std::string getWorldReplayDirPath(const std::string& worldSavePath);

		void setSavePath(const std::string& newSavePath);
		void pushSavePath(const std::string& newSavePath);
		void popSavePath();

		glm::ivec2 toChunkCoords(const glm::vec3& worldCoordinates);

		void givePlayerBlock(Ecs::EntityId player, int blockId, int blockCount);
		bool isPlayerUnderwater();
		bool isLoaded();

		Ecs::EntityId getLocalPlayer();
		void setLocalPlayer(Ecs::EntityId localPlayer);
		Ecs::EntityId createPlayer(const char* playerName, const glm::vec3& position);

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
		extern float deltaTime;
		extern std::string localPlayerName;

		extern bool doDaylightCycle;
	}
}

#endif