#include "network/ServerWorld.h"
#include "world/ChunkManager.h"
#include "world/BlockMap.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Camera.h"
#include "renderer/Font.h"
#include "renderer/Cubemap.h"
#include "renderer/Renderer.h"
#include "renderer/Frustum.h"
#include "renderer/Styles.h"
#include "input/Input.h"
#include "input/KeyHandler.h"
#include "core/Application.h"
#include "core/File.h"
#include "core/Ecs.h"
#include "core/Scene.h"
#include "core/Components.h"
#include "core/TransformSystem.h"
#include "core/Window.h"
#include "core/AppData.h"
#include "physics/Physics.h"
#include "physics/PhysicsComponents.h"
#include "gameplay/PlayerController.h"
#include "gameplay/CharacterController.h"
#include "gameplay/CharacterSystem.h"
#include "gameplay/Inventory.h"
#include "utils/TexturePacker.h"
#include "utils/DebugStats.h"
#include "utils/CMath.h"
#include "gui/Gui.h"
#include "gui/MainHud.h"
#include "network/Network.h"

namespace Minecraft
{
	struct Camera;
	struct Texture;
	class Frustum;

	namespace ServerWorld
	{
		std::string savePath = "";
		uint32 seed = UINT32_MAX;
		std::atomic<float> seedAsFloat = 0.0f;
		std::string chunkSavePath = "";
		int worldTime = 0;
		bool doDaylightCycle = false;

		// Members
		static robin_hood::unordered_set<glm::ivec2> loadedChunkPositions;
		static Ecs::Registry* registry;
		static glm::vec2 lastPlayerLoadPosition;

		void init(Ecs::Registry& sceneRegistry, const Transform& playerTransform)
		{
			// Net Code only lives inside of a world, that way we can initialize it with the appropriate
			// connection settings and everything
			Network::init(true, "127.0.0.1", 8080);

			// Initialize memory
			registry = &sceneRegistry;
			g_logger_assert(savePath != "", "<Server>: World save path must not be empty.");

			// Initialize and create any filepaths for save information
			savePath = (std::filesystem::path(AppData::worldsRootPath) / std::filesystem::path(savePath)).string();
			File::createDirIfNotExists(savePath.c_str());
			chunkSavePath = (savePath / std::filesystem::path("chunks")).string();
			g_logger_info("<Server>: World save folder at: %s", savePath.c_str());
			File::createDirIfNotExists(chunkSavePath.c_str());

			// Generate a seed if needed
			srand((unsigned long)time(NULL));
			if (File::isFile(getWorldDataFilepath(savePath).c_str()))
			{
				if (!deserialize())
				{
					g_logger_error("<Server>: Could not load world. World.bin has been corrupted or does not exist.");
					return;
				}
			}

			if (seed == UINT32_MAX)
			{
				// Generate a seed between -INT32_MAX and INT32_MAX
				seed = (uint32)(((float)rand() / (float)RAND_MAX) * UINT32_MAX);
			}
			seedAsFloat = (float)((double)seed / (double)UINT32_MAX) * 2.0f - 1.0f;
			srand(seed);
			g_logger_info("<Server>: World seed: %u", seed);
			g_logger_info("<Server>: World seed (as float): %2.8f", seedAsFloat.load());

			ChunkManager::init();
			lastPlayerLoadPosition = glm::vec2(playerTransform.position.x, playerTransform.position.z);
			ChunkManager::checkChunkRadius(playerTransform.position);
		}

		void free()
		{
			Network::free();

			serialize();
			ChunkManager::serialize();
			ChunkManager::free();
		}

		void update(float dt)
		{
			// Update all systems
			Network::update(dt);
			Physics::update(*registry, dt);
			CharacterSystem::update(*registry, dt);
			// TODO: Figure out the best way to keep transform forward, right, up vectors correct
			TransformSystem::update(*registry, dt);

			DebugStats::numDrawCalls = 0;
			static uint32 ticks = 0;
			ticks++;
			if (ticks > 10)
			{
				DebugStats::lastFrameTime = dt;
				ticks = 0;
			}

			if (doDaylightCycle)
			{
				worldTime = (worldTime + 10) % 2400;
			}
			int sunXRotation;
			// Since 0-180 is daytime, but 600-1800 is daytime in actual units, we need to offset our time here
			// 120 degrees - 300 degrees is daytime
			if (worldTime >= 600 && worldTime <= 1800)
			{
				sunXRotation = (int)CMath::mapRange((float)worldTime, 600.0f, 1800.0f, -45.0f, 235.0f);
				if (sunXRotation < 0)
				{
					sunXRotation = 360 - sunXRotation;
				}
			}
			else if (worldTime > 1800)
			{
				sunXRotation = (int)CMath::mapRange((float)worldTime, 1800.0f, 2400.0f, 235.0f, 240.0f);
			}
			else
			{
				sunXRotation = (int)CMath::mapRange((float)worldTime, 0.0f, 600.0f, 240.0f, 315.0f);
			}
		}

		void givePlayerBlock(int blockId, int blockCount, Ecs::EntityId playerId)
		{
			Inventory& inventory = registry->getComponent<Inventory>(playerId);
			for (int i = 0; i < Player::numHotbarSlots; i++)
			{
				if (!inventory.slots[i].blockId)
				{
					inventory.slots[i].blockId = blockId;
					inventory.slots[i].count = blockCount;
					break;
				}
			}
		}

		void serialize()
		{
			std::string filepath = getWorldDataFilepath(savePath);
			FILE* fp = fopen(filepath.c_str(), "wb");
			if (!fp)
			{
				g_logger_error("Could not serialize file '%s'", filepath.c_str());
				return;
			}

			// Write data
			fwrite(&seed, sizeof(uint32), 1, fp);
			fclose(fp);
		}

		bool deserialize()
		{
			std::string filepath = getWorldDataFilepath(savePath);
			FILE* fp = fopen(filepath.c_str(), "rb");
			if (!fp)
			{
				g_logger_error("Could not open file '%s'", filepath.c_str());
				return false;
			}

			// Read data
			fread(&seed, sizeof(uint32), 1, fp);
			fclose(fp);

			return true;
		}

		std::string getWorldDataFilepath(const std::string& worldSavePath)
		{
			return worldSavePath + "/world.bin";
		}
	}
}