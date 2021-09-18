#include "core.h"
#include "world/World.h"
#include "world/ChunkManager.h"
#include "world/BlockMap.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Camera.h"
#include "renderer/Font.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "core/Input.h"
#include "core/Application.h"
#include "core/File.h"
#include "core/Ecs.h"
#include "core/Components.h"
#include "core/TransformSystem.h"
#include "physics/Physics.h"
#include "physics/PhysicsComponents.h"
#include "gameplay/PlayerController.h"
#include "utils/TexturePacker.h"
#include "utils/DebugStats.h"

namespace Minecraft
{
	namespace World
	{
		// Internal declarations
		static void loadWorldTexture();
		static void checkChunkRadius();
		static void synchronizeChunks();
		static Chunk* findChunk(const glm::ivec2& chunkCoords);
		static void setUnloadedChunk(const Chunk& chunkVal);
		static void setChunk(const glm::ivec2& chunkCoords, const Chunk& chunkVal);
		static void drawDebugStats();

		// Members
		struct Members
		{
			RenderState renderState;
			Camera camera;
			PlayerController playerController;
			Shader shader;
			Texture worldTexture;
			std::unordered_set<glm::ivec2> loadedChunkPositions;
			Chunk chunks[World::ChunkCapacity];
			std::string worldSavePath;
			Ecs::Registry* registry;

			int32 seed;
		};

		static RandomController ctlr;

		static Members& obj();

		void init(Ecs::Registry& registry)
		{
			Members& m = obj();
			m.registry = &registry;
			m.worldSavePath = "world";

			srand((unsigned long)time(NULL));
			m.seed = (int32)(((float)rand() / (float)RAND_MAX) * 1000.0f);
			g_logger_info("World seed: %d", m.seed);

			// Initialize blocks
			//TexturePacker::packTextures("C:/dev/C++/MinecraftClone/assets/images/block", "textureFormat.yaml");
			g_memory_zeroMem(m.chunks, sizeof(Chunk) * ChunkCapacity);

			BlockMap::loadBlocks("textureFormat.yaml", "blockFormats.yaml");
			BlockMap::uploadTextureCoordinateMapToGpu();
			m.shader.compile("C:/dev/C++/MinecraftClone/assets/shaders/default.glsl");
			loadWorldTexture();

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_BUFFER, BlockMap::getTextureCoordinatesTextureId());
			m.shader.uploadInt("uTexCoordTexture", 1);

			// Create a chunk
			File::createDirIfNotExists(m.worldSavePath.c_str());
			Chunk::info();

			// Setup camera
			Ecs::EntityId cameraEntity = m.registry->createEntity();
			m.registry->addComponent<Transform>(cameraEntity);
			Transform& cameraTransform = m.registry->getComponent<Transform>(cameraEntity);
			cameraTransform.position = glm::vec3(0, 257.0f, 1.0f);
			cameraTransform.orientation = glm::vec3(0.0f, 0.0f, 0.0f);
			m.camera.fov = 45.0f;
			m.camera.cameraEntity = cameraEntity;

			Renderer::setCamera(m.camera);

			// Setup player
			Ecs::EntityId player = m.registry->createEntity();
			m.registry->addComponent<Transform>(player);
			//m.registry->addComponent<BoxCollider>(player);
			//m.registry->addComponent<Rigidbody>(player);
			//BoxCollider& boxCollider = m.registry->getComponent<BoxCollider>(player);
			//boxCollider.size.x = 0.5f;
			//boxCollider.size.y = 3.0f;
			//boxCollider.size.z = 0.75f;
			Transform& transform = m.registry->getComponent<Transform>(player);
			transform.position.y = 255;
			transform.position.x = 45.0f;
			transform.position.z = -45.0f;
			m.playerController.init(player);

			// Setup random physics entity
			Ecs::EntityId randomEntity = m.registry->createEntity();
			m.registry->addComponent<Transform>(randomEntity);
			m.registry->addComponent<BoxCollider>(randomEntity);
			m.registry->addComponent<Rigidbody>(randomEntity);
			m.registry->addComponent<RandomController>(randomEntity);
			BoxCollider& boxCollider2 = m.registry->getComponent<BoxCollider>(randomEntity);
			boxCollider2.size.x = 0.5f;
			boxCollider2.size.y = 3.0f;
			boxCollider2.size.z = 0.75f;
			Transform& transform2 = m.registry->getComponent<Transform>(randomEntity);
			transform2.position.y = 255;
			transform2.position.x = 45.0f;
			transform2.position.z = -45.0f;
			m.registry->getComponent<RandomController>(randomEntity).init(randomEntity);
			ctlr = m.registry->getComponent<RandomController>(randomEntity);

			ChunkManager::init(m.seed);
			checkChunkRadius();

			Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);
		}

		glm::ivec2 toChunkCoords(const glm::vec3& worldCoordinates)
		{
			return {
				glm::floor(worldCoordinates.x / 16.0f),
				glm::floor(worldCoordinates.z / 16.0f)
			};
		}

		Chunk& getChunk(const glm::vec3& worldPosition)
		{
			Members& m = obj();
			glm::ivec2 chunkPosition = toChunkCoords(worldPosition);
			for (Chunk& chunk : m.chunks)
			{
				if (chunk.chunkCoordinates == chunkPosition)
				{
					return chunk;
				}
			}

			return Chunk{};
		}

		Block getBlock(const glm::vec3& worldPosition)
		{
			Members& m = obj();
			Chunk& chunk = getChunk(worldPosition);
			if (chunk.chunkData)
			{
				glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunk.chunkCoordinates.x * 16.0f, 0.0f, chunk.chunkCoordinates.y * 16.0f));
				return chunk.getLocalBlock(localPosition);
			}
			return BlockMap::NULL_BLOCK;
		}

		void update(float dt)
		{
			Members& m = obj();

			static bool showDebugStats = false;
			if (showDebugStats)
			{
				drawDebugStats();
			}
			DebugStats::numDrawCalls = 0;
			DebugStats::lastFrameTime = dt;

			// Update all systems
			Physics::update(*m.registry, dt);
			m.playerController.update(dt, *m.registry);
			ctlr.update(dt, *m.registry);
			m.registry->getComponent<Transform>(m.camera.cameraEntity).position =
				m.registry->getComponent<Transform>(m.playerController.playerId).position;
			m.registry->getComponent<Transform>(m.camera.cameraEntity).orientation =
				m.registry->getComponent<Transform>(m.playerController.playerId).orientation;
			// TODO: Figure out the best way to keep transform forward, right, up vectors correct
			TransformSystem::update(dt, *m.registry);

			// Update camera calculations
			m.shader.bind();
			m.shader.uploadMat4("uProjection", m.camera.calculateProjectionMatrix(*m.registry));
			m.shader.uploadMat4("uView", m.camera.calculateViewMatrix(*m.registry));
			m.shader.uploadVec3("uSunPosition", glm::vec3{ 1, 355, 1 });

			if (Input::keyBeginPress(GLFW_KEY_F2))
			{
				static bool lockCursor = false;
				lockCursor = !lockCursor;
				Application::lockCursor(lockCursor);
			}
			if (Input::keyBeginPress(GLFW_KEY_F3))
			{
				showDebugStats = !showDebugStats;
			}

			glActiveTexture(GL_TEXTURE0);
			m.worldTexture.bind();
			m.shader.uploadInt("uTexture", 0);

			const glm::vec3& playerPosition = m.registry->getComponent<Transform>(m.playerController.playerId).position;
			glm::ivec2 playerPositionInChunkCoords = toChunkCoords(playerPosition);
			for (int i = 0; i < ChunkCapacity; i++)
			{
				Chunk& chunk = m.chunks[i];
				if (chunk.loaded)
				{
					m.shader.uploadIVec2("uChunkPos", chunk.chunkCoordinates);
					m.shader.uploadVec3("uPlayerPosition", playerPosition);
					m.shader.uploadInt("uChunkRadius", World::ChunkRadius);
					chunk.render();

					const glm::ivec2 localChunkPos = chunk.chunkCoordinates - playerPositionInChunkCoords;
					bool hasBeenQueued = m.loadedChunkPositions.find(chunk.chunkCoordinates) == m.loadedChunkPositions.end();
					if (!hasBeenQueued && (localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) >= ChunkRadius * ChunkRadius)
					{
						ChunkManager::queueDeleteChunk(chunk);
						m.loadedChunkPositions.erase(chunk.chunkCoordinates);
					}

					DebugStats::numDrawCalls++;
				}
			}

			// Do line rendering type stuff
			Renderer::render();

			checkChunkRadius();
			synchronizeChunks();
		}

		void cleanup()
		{
			Members& m = obj();
			ChunkManager::free();

			for (int i = 0; i < ChunkCapacity; i++)
			{
				Chunk& chunk = m.chunks[i];
				if (chunk.loaded)
				{
					chunk.freeCpu();
					chunk.freeGpu();
				}
			}

			m.registry->free();
		}

		static bool exists(const glm::ivec2& position)
		{
			Members& m = obj();
			for (Chunk& chunk : m.chunks)
			{
				if (chunk.chunkCoordinates == position)
				{
					return true;
				}
			}

			return false;
		}

		static void checkChunkRadius()
		{
			Members& m = obj();
			const glm::vec3 playerPosition = m.registry->getComponent<Transform>(m.playerController.playerId).position;

			// Position changes as we circle out
			// See switch statement below
			glm::ivec2 position = toChunkCoords(playerPosition);
			glm::ivec2 playerPosChunkCoords = position;

			bool needsWork = false;
			// 0 RIGHT, 1 UP, 2 LEFT, 3 DOWN
			uint8 direction = 0;
			uint8 distance = 1;
			uint8 timeToChangeDistance = 0;
			uint8 travelled = 0;
			while (true)
			{
				glm::ivec2 localPos = playerPosChunkCoords - position;
				if ((localPos.x * localPos.x) + (localPos.y * localPos.y) < ChunkRadius * ChunkRadius)
				{
					bool alreadyExists = exists(position);
					auto iter = m.loadedChunkPositions.find(position);
					if (!alreadyExists && iter == m.loadedChunkPositions.end())
					{
						ChunkManager::queueCreateChunk(position.x, position.y);
						m.loadedChunkPositions.insert(position);
					}
				}

				switch (direction)
				{
				case 0:
					position.x++;
					break;
				case 1:
					position.y++;
					break;
				case 2:
					position.x--;
					break;
				case 3:
					position.y--;
					break;
				}

				travelled++;
				if (travelled >= distance)
				{
					direction = (direction + 1) % 4;
					travelled = 0;

					timeToChangeDistance++;
					if (timeToChangeDistance > 1)
					{
						timeToChangeDistance = 0;
						distance++;
					}

					if (distance > ChunkRadius * 2)
					{
						break;
					}
				}
			}
		}

		static void synchronizeChunks()
		{
			Members& m = obj();
			std::vector<Chunk> readyChunks = ChunkManager::getReadyChunks();
			if (readyChunks.size() > 0)
			{
				for (Chunk& chunk : readyChunks)
				{
					if (chunk.loaded)
					{
						g_logger_info("Unloading from GPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
						chunk.freeGpu();
						setChunk(chunk.chunkCoordinates, chunk);
					}
					else
					{
						g_logger_info("Uploading to GPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
						chunk.uploadToGPU();
						setUnloadedChunk(chunk);
					}
				}
			}
		}

		static void loadWorldTexture()
		{
			Members& m = obj();
			m.worldTexture = TextureBuilder()
				.setFormat(ByteFormat::RGBA8_UI)
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setFilepath("test.png")
				.generate(true);

			// Upload the world texture
			glActiveTexture(GL_TEXTURE0);
			m.worldTexture.bind();
			m.shader.uploadInt("uTexture", 0);

			glUseProgram(m.shader.programId);
		}

		static Chunk* findChunk(const glm::ivec2& chunkCoords)
		{
			Members& m = obj();
			for (int i = 0; i < ChunkCapacity; i++)
			{
				if (m.chunks[i].chunkCoordinates == chunkCoords)
				{
					return &m.chunks[i];
				}
			}

			return nullptr;
		}

		static void setUnloadedChunk(const Chunk& chunkVal)
		{
			Members& m = obj();
			for (int i = 0; i < ChunkCapacity; i++)
			{
				if (!m.chunks[i].loaded)
				{
					m.chunks[i] = chunkVal;
					return;
				}
			}

			g_logger_warning("Tried to set unloaded chunk, but there are no unloaded chunks left.");
		}

		static void setChunk(const glm::ivec2& chunkCoords, const Chunk& chunkVal)
		{
			Members& m = obj();
			for (int i = 0; i < ChunkCapacity; i++)
			{
				if (m.chunks[i].chunkCoordinates == chunkCoords)
				{
					m.chunks[i] = chunkVal;
					return;
				}
			}

			g_logger_warning("Tried to set chunk<%d, %d> but could not find matching chunk.", chunkCoords.x, chunkCoords.y);
		}

		static void drawDebugStats()
		{
			Font* font = Fonts::getFont("assets/fonts/Minecraft.ttf", 16_px);
			if (font)
			{
				float textScale = 0.4f;
				float textHzPadding = 0.1f;
				Style transparentSquare = Styles::defaultStyle;
				transparentSquare.color = "#00000055"_hex;

				glm::vec2 drawCallPos = glm::vec2(-2.95f, 1.35f);
				std::string drawCallStr = std::string("Draw calls: " + std::to_string(DebugStats::numDrawCalls));
				Renderer::drawString(
					drawCallStr,
					*font,
					drawCallPos,
					textScale,
					Styles::defaultStyle);
				
				glm::vec2 fpsPos = glm::vec2(-2.1f, 1.35f);
				std::string fpsStr = std::string("FPS: " + std::to_string(1.0f / DebugStats::lastFrameTime));
				Renderer::drawString(
					fpsStr,
					*font,
					fpsPos,
					textScale,
					Styles::defaultStyle);

				glm::vec2 frameTimePos = glm::vec2(-1.25f, 1.35f);;
				std::string frameTimeStr = std::string("1/FPS: " + std::to_string(DebugStats::lastFrameTime));
				Renderer::drawString(
					frameTimeStr,
					*font,
					frameTimePos,
					textScale,
					Styles::defaultStyle);

				Renderer::drawFilledSquare2D(drawCallPos - glm::vec2(0.02f, 0.01f), glm::vec2(2.6f, 0.1f), transparentSquare, -1);
			}
		}

		static Members& obj()
		{
			static Members memberData{};
			return memberData;
		}
	}
}