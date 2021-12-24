#include "world/ChunkManager.h"
#include "world/World.h"
#include "world/BlockMap.h"
#include "world/Chunk.hpp"
#include "world/TerrainGenerator.h"
#include "core/Pool.hpp"
#include "core/File.h"
#include "utils/DebugStats.h"
#include "utils/CMath.h"
#include "utils/Constants.h"
#include "renderer/Shader.h"
#include "renderer/Renderer.h"
#include "renderer/Frustum.h"
#include "renderer/Framebuffer.h"
#include "renderer/Texture.h"
#include "core/Application.h"
#include "network/Network.h"
#include "world/ChunkThreadWorker.h"

namespace Minecraft
{
	namespace ChunkManager
	{
		struct DrawCommand
		{
			DrawArraysIndirectCommand command;
			int distanceToPlayer;
			int level;
		};

		namespace DrawCommandUtil
		{
			bool operator<(const DrawCommand& a, const DrawCommand& b)
			{
				return a.distanceToPlayer < b.distanceToPlayer;
			}

			static bool operator>(const DrawCommand& a, const DrawCommand& b)
			{
				return a.distanceToPlayer > b.distanceToPlayer;
			}
		}

		class CommandBufferContainer
		{
		public:
			CommandBufferContainer(int maxNumCommands, bool isTransparent)
			{
				this->maxNumCommands = maxNumCommands;
				this->isTransparent = isTransparent;
				commandBuffer = nullptr;
				chunkPosBuffer = nullptr;
				biomeBuffer = nullptr;
				numCommands = 0;
			}

			void init()
			{
				this->commandBuffer = (DrawCommand*)g_memory_allocate(sizeof(DrawCommand) * maxNumCommands);
				this->chunkPosBuffer = (int32*)g_memory_allocate(sizeof(int32) * maxNumCommands * 2);
				this->biomeBuffer = (int32*)g_memory_allocate(sizeof(int32) * maxNumCommands);
				this->numCommands = 0;
			}

			void free()
			{
				if (commandBuffer)
				{
					g_memory_free(commandBuffer);
					commandBuffer = nullptr;
				}

				if (chunkPosBuffer)
				{
					g_memory_free(chunkPosBuffer);
					chunkPosBuffer = nullptr;
				}

				if (biomeBuffer)
				{
					g_memory_free(biomeBuffer);
					biomeBuffer = nullptr;
				}
			}

			void add(const DrawArraysIndirectCommand& command, const glm::ivec2& chunkCoords, int level, const glm::ivec2& playerPosChunkCoords, int biome)
			{
				g_logger_assert((numCommands + 1) < maxNumCommands, "Ran out of room in command buffer!");
				glm::ivec2 d = chunkCoords - playerPosChunkCoords;
				int dSquared = (d.x * d.x) + (d.y * d.y);
				commandBuffer[numCommands] = { command, dSquared, level };
				commandBuffer[numCommands].command.baseInstance = numCommands;
				chunkPosBuffer[(numCommands * 2)] = chunkCoords.x;
				chunkPosBuffer[(numCommands * 2) + 1] = chunkCoords.y;
				biomeBuffer[numCommands] = biome;
				numCommands++;
			}

			void sort(const glm::ivec2& playerPosChunkCoords)
			{
				if (!isTransparent)
				{
					// Sort chunks front to back
					std::sort(commandBuffer, commandBuffer + numCommands, DrawCommandUtil::operator<);
				}
				else
				{
					std::sort(commandBuffer, commandBuffer + numCommands, DrawCommandUtil::operator>);
				}
			}

			inline int getNumCommands() const
			{
				return numCommands;
			}

			inline const DrawCommand* getCommandBuffer() const
			{
				return commandBuffer;
			}

			inline const int32* getChunkPosBuffer() const
			{
				return chunkPosBuffer;
			}

			inline const int32* getBiomeBuffer() const
			{
				return biomeBuffer;
			}

			inline void softReset()
			{
				numCommands = 0;
			}

		private:
			int maxNumCommands;
			int numCommands;
			bool isTransparent;
			DrawCommand* commandBuffer;
			int32* chunkPosBuffer;
			int32* biomeBuffer;
		};

		// Internal functions
		static void retesselateChunkBlockUpdate(const glm::ivec2& chunkCoords, const glm::vec3& worldPosition, Chunk* blockData);

		// Internal variables
		static std::mutex chunkMtx;
		static uint32 processorCount = 0;
		static robin_hood::unordered_node_map<glm::ivec2, Chunk> chunks = {};

		static uint32 chunkPosInstancedBuffer;
		static uint32 biomeInstancedVbo;
		static uint32 globalVao;
		static uint32 globalRenderVbo;
		// TODO: Make this better
		static uint32 solidDrawCommandVbo;
		static uint32 blendableDrawCommandVbo;
		static Shader compositeShader;

		static ChunkThreadWorker* chunkWorker = nullptr;
		static Pool<SubChunk>* subChunks = nullptr;
		static Pool<Block>* blockPool = nullptr;
		static CommandBufferContainer* solidCommandBuffer = nullptr;
		static CommandBufferContainer* blendableCommandBuffer = nullptr;

		void init()
		{
			// A chunk uses 55,000 vertices on average, so a sub-chunk can be estimated to use about 
			// 4,500 vertices on average. That's the default vertex bucket size
			processorCount = 1;// std::thread::hardware_concurrency();

			// Initialize the singletons
			chunkWorker = new ChunkThreadWorker();
			subChunks = new Pool<SubChunk>(1, World::ChunkCapacity * 16);
			blockPool = new Pool<Block>(World::ChunkDepth * World::ChunkWidth * World::ChunkHeight, World::ChunkCapacity);
			solidCommandBuffer = new CommandBufferContainer(subChunks->size(), false);
			blendableCommandBuffer = new CommandBufferContainer(subChunks->size(), true);
			chunks.clear();

			compositeShader.compile("assets/shaders/CompositeShader.glsl");

			// Set up draw commands to relate to our sub chunks
			solidCommandBuffer->init();
			glCreateBuffers(1, &solidDrawCommandVbo);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, solidDrawCommandVbo);
			glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(DrawCommand) * subChunks->size(), NULL, GL_DYNAMIC_DRAW);

			blendableCommandBuffer->init();
			glCreateBuffers(1, &blendableDrawCommandVbo);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, blendableDrawCommandVbo);
			glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(DrawCommand) * subChunks->size(), NULL, GL_DYNAMIC_DRAW);

			// Initialize the SubChunks
			// Generate a bunch of empty vertex buckets for GPU use
			glCreateVertexArrays(1, &globalVao);
			glBindVertexArray(globalVao);

			glGenBuffers(1, &globalRenderVbo);
			glBindBuffer(GL_ARRAY_BUFFER, globalRenderVbo);

			size_t totalSizeOfSubChunkVertices = subChunks->size() * World::MaxVertsPerSubChunk * sizeof(Vertex);

			// Set our vertex attribute pointers
			glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(Vertex), (void*)offsetof(Vertex, data1));
			glVertexAttribDivisor(0, 0);
			glEnableVertexAttribArray(0);

			glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(Vertex), (void*)(offsetof(Vertex, data2)));
			glVertexAttribDivisor(1, 0);
			glEnableVertexAttribArray(1);

			// Set up our global immutable buffer
			GLbitfield flags = GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT;
			glBufferStorage(GL_ARRAY_BUFFER, totalSizeOfSubChunkVertices, NULL, flags);
			Vertex* basePointer = (Vertex*)glMapBufferRange(GL_ARRAY_BUFFER, 0, subChunks->size() * World::MaxVertsPerSubChunk, flags);
			for (uint32 i = 0; i < subChunks->size(); i++)
			{
				// Assign the pointers for the data on the CPU
				(*subChunks)[i]->first = (i * World::MaxVertsPerSubChunk);
				(*subChunks)[i]->data = basePointer + (*subChunks)[i]->first;
				(*subChunks)[i]->numVertsUsed = 0;
				(*subChunks)[i]->drawCommandIndex = i;
				(*subChunks)[i]->state = SubChunkState::Unloaded;
			}

			// Set up the instanced chunk pos vertex buffer
			glCreateBuffers(1, &chunkPosInstancedBuffer);
			glBindBuffer(GL_ARRAY_BUFFER, chunkPosInstancedBuffer);
			glBufferData(GL_ARRAY_BUFFER, sizeof(int32) * 2 * subChunks->size(), NULL, GL_DYNAMIC_DRAW);

			glVertexAttribIPointer(10, 2, GL_INT, sizeof(int32) * 2, 0);
			glVertexAttribDivisor(10, 1);
			glEnableVertexAttribArray(10);

			// Set up biome buffer
			glCreateBuffers(1, &biomeInstancedVbo);
			glBindBuffer(GL_ARRAY_BUFFER, biomeInstancedVbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(int32) * subChunks->size(), NULL, GL_DYNAMIC_DRAW);

			glVertexAttribIPointer(11, 1, GL_INT, sizeof(int32), 0);
			glVertexAttribDivisor(11, 1);
			glEnableVertexAttribArray(11);

			// Subchunk = 16x16x16  Blocks
			// BigChunk = 16x256x16 Blocks
			g_logger_info("Vertex Pool Total Size: %2.3f Gb", (float)(totalSizeOfSubChunkVertices / (1024.0f * 1024 * 1024)));
			g_logger_info("Block Pool Total Size: %2.3f Gb", (float)(blockPool->totalSize() / (1024.0f * 1024 * 1024)));
			DebugStats::totalChunkRamAvailable = totalSizeOfSubChunkVertices + (float)blockPool->totalSize();
		}

		void free()
		{
			// Delete GPU memory
			// TODO: Do error checking on these VBOs to ensure they are valid
			glDeleteBuffers(1, &solidDrawCommandVbo);
			glDeleteBuffers(1, &blendableDrawCommandVbo);

			chunks.clear();

			glDeleteBuffers(1, &globalRenderVbo);
			glDeleteBuffers(1, &chunkPosInstancedBuffer);
			glDeleteBuffers(1, &biomeInstancedVbo);
			glDeleteVertexArrays(1, &globalVao);

			// Delete CPU memory
			if (chunkWorker)
			{
				chunkWorker->free();
				delete chunkWorker;
				chunkWorker = nullptr;
			}

			if (subChunks)
			{
				delete subChunks;
				subChunks = nullptr;
			}

			if (blockPool)
			{
				delete blockPool;
				blockPool = nullptr;
			}

			if (solidCommandBuffer)
			{
				solidCommandBuffer->free();
				delete solidCommandBuffer;
				solidCommandBuffer = nullptr;
			}

			if (blendableCommandBuffer)
			{
				blendableCommandBuffer->free();
				delete blendableCommandBuffer;
				blendableCommandBuffer = nullptr;
			}

			compositeShader.destroy();
		}

		void serialize()
		{
			for (robin_hood::pair<const glm::ivec2, Chunk>& chunkIter : chunks)
			{
				Chunk& chunk = chunkIter.second;
				Block* blockData = chunk.data;
				if (chunk.state != ChunkState::Saving && blockData &&
					chunk.state != ChunkState::Unloaded &&
					chunk.state != ChunkState::Unloading)
				{
					queueSaveChunk(chunkIter.first);
				}
			}
		}

		robin_hood::unordered_node_map<glm::ivec2, Chunk>& getAllChunks()
		{
			return chunks;
		}

		void queueCreateChunk(const glm::ivec2& chunkCoordinates)
		{
			// Only upload if we need to
			Chunk* chunk = getChunk(chunkCoordinates);
			if (!chunk)
			{
				if (!blockPool->empty())
				{
					Chunk newChunk;
					newChunk.data = blockPool->getNewPool();

					newChunk.chunkCoords = chunkCoordinates;
					newChunk.topNeighbor = getChunk(chunkCoordinates + INormals2::Up);
					newChunk.bottomNeighbor = getChunk(chunkCoordinates + INormals2::Down);
					newChunk.leftNeighbor = getChunk(chunkCoordinates + INormals2::Left);
					newChunk.rightNeighbor = getChunk(chunkCoordinates + INormals2::Right);
					newChunk.state = ChunkState::Loaded;

					{
						// TODO: Ensure this is only ever accessed from the main thread
						//std::lock_guard lock(chunkMtx);
						chunks[newChunk.chunkCoords] = newChunk;
					}

					FillChunkCommand cmd;
					cmd.type = CommandType::GenerateTerrain;
					cmd.chunk = &chunks[newChunk.chunkCoords];
					cmd.subChunks = subChunks;
					cmd.isRetesselating = false;

					// Queue the fill command
					chunkWorker->queueCommand(cmd);
					// Queue the tesselate command
					cmd.type = CommandType::TesselateVertices;
					chunkWorker->queueCommand(cmd);

					DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed + blockPool->poolSize() * sizeof(Block);
				}
				else
				{
					// What do we do if there were no free blocks?
					g_logger_warning("No free pools for block data.");
				}
			}
		}

		void queueCalculateLighting(const glm::ivec2& lastPlayerPosInChunkCoords)
		{
			FillChunkCommand cmd;
			cmd.type = CommandType::CalculateLighting;
			cmd.playerPosChunkCoords = lastPlayerPosInChunkCoords;
			chunkWorker->queueCommand(cmd);
		}

		float percentWorkDone()
		{
			return chunkWorker->percentDone();
		}

		void queueRecalculateLighting(const glm::ivec2& chunkCoordinates, const glm::vec3& blockPositionThatUpdated, bool removedLightSource)
		{
			// Only recalculate if we need to
			Chunk* chunk = getChunk(chunkCoordinates);
			if (chunk)
			{
				FillChunkCommand cmd;
				cmd.type = CommandType::RecalculateLighting;
				cmd.subChunks = subChunks;
				cmd.chunk = chunk;
				cmd.blockThatUpdated = blockPositionThatUpdated;
				cmd.removedLightSource = removedLightSource;

				chunkWorker->queueCommand(cmd);
			}
		}

		void queueRetesselateChunk(const glm::ivec2& chunkCoordinates, Chunk* chunk)
		{
			if (!chunk)
			{
				chunk = getChunk(chunkCoordinates);
			}

			if (chunk)
			{
				FillChunkCommand cmd;
				cmd.type = CommandType::TesselateVertices;
				cmd.subChunks = subChunks;
				cmd.chunk = chunk;
				cmd.isRetesselating = true;

				// Update the sub-chunks that are about to be deleted
				bool needsToBeRetesselated = false;
				for (int i = 0; i < (int)subChunks->size(); i++)
				{
					if ((*subChunks)[i]->chunkCoordinates == chunkCoordinates && (*subChunks)[i]->state == SubChunkState::Uploaded)
					{
						(*subChunks)[i]->state = SubChunkState::RetesselateVertices;
						needsToBeRetesselated = true;
					}
				}

				chunkWorker->queueCommand(cmd);
			}
		}

		void queueSaveChunk(const glm::ivec2& chunkCoordinates)
		{
			// TODO: This could be bad... Maybe we should return a copy of the ChunkStateData instead of a pointer
			// because it's very possible that the pointer could become invalid 

			// Only save if we need to
			Chunk* chunk = getChunk(chunkCoordinates);
			if (chunk)
			{
				if (chunk->state != ChunkState::Saving && chunk->data)
				{
					chunk->state = ChunkState::Saving;
					FillChunkCommand cmd;
					cmd.type = CommandType::SaveBlockData;
					cmd.chunk = chunk;
					cmd.subChunks = subChunks;
					chunkWorker->queueCommand(cmd);
				}
			}
		}

		void queueClientLoadChunk(void* chunkData, const glm::ivec2& chunkCoordinates, ChunkState state)
		{
			// Only upload if we need to
			Chunk* chunk = getChunk(chunkCoordinates);
			if (!chunk)
			{
				if (!blockPool->empty())
				{
					Chunk newChunk;
					newChunk.data = blockPool->getNewPool();

					newChunk.chunkCoords = chunkCoordinates;
					newChunk.topNeighbor = getChunk(chunkCoordinates + INormals2::Up);
					newChunk.bottomNeighbor = getChunk(chunkCoordinates + INormals2::Down);
					newChunk.leftNeighbor = getChunk(chunkCoordinates + INormals2::Left);
					newChunk.rightNeighbor = getChunk(chunkCoordinates + INormals2::Right);
					newChunk.state = state;

					{
						// TODO: Ensure this is only ever accessed from the main thread
						//std::lock_guard lock(chunkMtx);
						chunks[newChunk.chunkCoords] = newChunk;
					}

					FillChunkCommand cmd;
					cmd.type = CommandType::ClientLoadChunk;
					cmd.chunk = &chunks[newChunk.chunkCoords];
					cmd.subChunks = subChunks;
					cmd.clientChunkData = chunkData;

					// Queue the fill command
					chunkWorker->queueCommand(cmd);
					// Queue the calculate lighting command
					cmd.type = CommandType::CalculateLighting;
					chunkWorker->queueCommand(cmd);
					// Queue the tesselate command
					cmd.type = CommandType::TesselateVertices;
					chunkWorker->queueCommand(cmd);

					DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed + blockPool->poolSize() * sizeof(Block);
				}
				else
				{
					// What do we do if there were no free blocks?
					g_logger_warning("No free pools for block data.");
				}
			}
		}

		void queueGenerateDecorations(const glm::ivec2& lastPlayerLoadChunkPos)
		{
			FillChunkCommand cmd;
			cmd.type = CommandType::GenerateDecorations;
			cmd.playerPosChunkCoords = lastPlayerLoadChunkPos;
			chunkWorker->queueCommand(cmd);
		}

		Block getBlock(const glm::vec3& worldPosition)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			Chunk* chunk = getChunk(worldPosition);

			if (!chunk)
			{
				// Assume it's a chunk that's out of bounds
				// TODO: Make this only return null block if it's far far away from the player
				return BlockMap::NULL_BLOCK;
			}

			return ChunkPrivate::getBlock(worldPosition, chunkCoords, chunk);
		}

		void setBlock(const glm::vec3& worldPosition, Block newBlock)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			Chunk* chunk = getChunk(worldPosition);

			if (!chunk)
			{
				if (worldPosition.y >= 0 && worldPosition.y < 256)
				{
					// Assume it's a chunk that's out of bounds
					g_logger_warning("Tried to set invalid block at position<%2.3f, %2.3f, %2.3f>!", worldPosition.x, worldPosition.y, worldPosition.z);
				}
				return;
			}

			if (ChunkPrivate::setBlock(worldPosition, chunkCoords, chunk, newBlock))
			{
				retesselateChunkBlockUpdate(chunkCoords, worldPosition, chunk);
				queueRecalculateLighting(chunkCoords, worldPosition, false);
				chunkWorker->beginWork();
			}
		}

		void removeBlock(const glm::vec3& worldPosition)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			Chunk* chunk = getChunk(worldPosition);

			if (!chunk)
			{
				if (worldPosition.y >= 0 && worldPosition.y < 256)
				{
					// Assume it's a chunk that's out of bounds
					g_logger_warning("Tried to set invalid block at position<%2.3f, %2.3f, %2.3f>!", worldPosition.x, worldPosition.y, worldPosition.z);
				}
				return;
			}

			bool isLightSourceBlock = ChunkManager::getBlock(worldPosition).isLightSource();
			if (ChunkPrivate::removeBlock(worldPosition, chunkCoords, chunk))
			{
				retesselateChunkBlockUpdate(chunkCoords, worldPosition, chunk);
				queueRecalculateLighting(chunkCoords, worldPosition, isLightSourceBlock);
				chunkWorker->beginWork();
			}
		}

		Chunk* getChunk(const glm::vec3& worldPosition)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			return getChunk(chunkCoords);
		}

		Chunk* getChunk(const glm::ivec2& chunkCoords)
		{
			Chunk* chunk = nullptr;

			{
				// TODO: Make this thread-safe somehow or something
				// or make sure no threads access this
				std::lock_guard<std::mutex> lock(chunkMtx);
				const robin_hood::unordered_map<glm::ivec2, Chunk>::iterator& iter = chunks.find(chunkCoords);
				if (iter != chunks.end())
				{
					chunk = &iter->second;
				}
			}

			return chunk;
		}

		void patchChunkPointers()
		{
			for (auto& pair : chunks)
			{
				Chunk& chunk = pair.second;
				auto iter1 = chunks.find(chunk.chunkCoords + INormals2::Up);
				chunk.topNeighbor = iter1 == chunks.end() ? nullptr : &iter1->second;
				auto iter2 = chunks.find(chunk.chunkCoords + INormals2::Down);
				chunk.bottomNeighbor = iter2 == chunks.end() ? nullptr : &iter2->second;
				auto iter3 = chunks.find(chunk.chunkCoords + INormals2::Left);
				chunk.leftNeighbor = iter3 == chunks.end() ? nullptr : &iter3->second;
				auto iter4 = chunks.find(chunk.chunkCoords + INormals2::Right);
				chunk.rightNeighbor = iter4 == chunks.end() ? nullptr : &iter4->second;
			}
		}

		void beginWork()
		{
			chunkWorker->beginWork();
		}

		void wakeUpCv2()
		{
			chunkWorker->wakeupCv2();
		}

		void render(const glm::vec3& playerPosition, const glm::ivec2& playerPositionInChunkCoords, Shader& opaqueShader, Shader& transparentShader, const Frustum& cameraFrustum)
		{
			chunkWorker->setPlayerPosChunkCoords(playerPositionInChunkCoords);

			for (int i = 0; i < (int)subChunks->size(); i++)
			{
				if ((*subChunks)[i]->state != SubChunkState::Unloaded)
				{
					glm::ivec2 chunkPos = (*subChunks)[i]->chunkCoordinates;
					const auto& iter = chunks.find(chunkPos);
					if (iter == chunks.end() && (*subChunks)[i]->state != SubChunkState::TesselatingVertices)
					{
						// If the chunk coords are no longer loaded, set this chunk as not in use anymore
						(*subChunks)[i]->state = SubChunkState::Unloaded;
						(*subChunks)[i]->numVertsUsed = 0;
						subChunks->freePool(i);
					}
					else if (iter != chunks.end() && iter->second.state == ChunkState::Loaded)
					{
						if ((*subChunks)[i]->state == SubChunkState::UploadVerticesToGpu)
						{
							g_logger_assert((*subChunks)[i]->numVertsUsed.load() > 0, "Sub Chunk should never have tried to upload 0 verts to GPU.");
							(*subChunks)[i]->state = SubChunkState::Uploaded;
						}

						if ((*subChunks)[i]->state == SubChunkState::Uploaded || (*subChunks)[i]->state == SubChunkState::RetesselateVertices)
						{
							g_logger_assert((*subChunks)[i]->numVertsUsed.load() > 0, "Sub Chunk should never have tried to upload 0 verts to GPU.");
							float yCenter = (float)(*subChunks)[i]->subChunkLevel * 16.0f;
							glm::vec3 chunkPos = glm::vec3((*subChunks)[i]->chunkCoordinates.x * World::ChunkDepth, yCenter, (*subChunks)[i]->chunkCoordinates.y * World::ChunkWidth);
							if (cameraFrustum.isBoxVisible(chunkPos, chunkPos + glm::vec3(16, 16, 16)))
							{
								DrawArraysIndirectCommand drawCommand;
								g_logger_assert((*subChunks)[i]->numVertsUsed.load() > 0, "Sub Chunk should never have tried to upload 0 verts to GPU.");
								drawCommand.baseInstance = 0;
								drawCommand.instanceCount = 1;
								drawCommand.count = (*subChunks)[i]->numVertsUsed;
								drawCommand.first = (*subChunks)[i]->first;
								if ((*subChunks)[i]->isBlendable)
								{
									blendableCommandBuffer->add(drawCommand, (*subChunks)[i]->chunkCoordinates, (*subChunks)[i]->subChunkLevel, playerPositionInChunkCoords, 0);
								}
								else
								{
									solidCommandBuffer->add(drawCommand, (*subChunks)[i]->chunkCoordinates, (*subChunks)[i]->subChunkLevel, playerPositionInChunkCoords, 0);
								}
							}

							if ((*subChunks)[i]->state == SubChunkState::DoneRetesselating)
							{
								(*subChunks)[i]->numVertsUsed = 0;
								(*subChunks)[i]->state = SubChunkState::Unloaded;
								subChunks->freePool(i);
							}
						}
					}
				}
			}

			glm::vec3 tint = glm::vec3(1.0f);
			if (World::isPlayerUnderwater())
			{
				tint = "#497dd1"_hex;
			}
			if (solidCommandBuffer->getNumCommands() > 0)
			{
				// Render opaque geometry
				glEnable(GL_CULL_FACE);
				glEnable(GL_DEPTH_TEST);
				glDepthFunc(GL_LESS);
				glDepthMask(GL_TRUE);
				glDisable(GL_BLEND);

				solidCommandBuffer->sort(playerPositionInChunkCoords);
				glBindBuffer(GL_ARRAY_BUFFER, chunkPosInstancedBuffer);
				glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(int32) * 2 * solidCommandBuffer->getNumCommands(), solidCommandBuffer->getChunkPosBuffer());
				glBindBuffer(GL_ARRAY_BUFFER, biomeInstancedVbo);
				glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(int32) * solidCommandBuffer->getNumCommands(), solidCommandBuffer->getBiomeBuffer());
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, solidDrawCommandVbo);
				glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(DrawCommand) * solidCommandBuffer->getNumCommands(), solidCommandBuffer->getCommandBuffer());
				DebugStats::numDrawCalls += solidCommandBuffer->getNumCommands();

				glBindVertexArray(globalVao);
				opaqueShader.bind();
				opaqueShader.uploadVec3("uPlayerPosition", playerPosition);
				opaqueShader.uploadInt("uChunkRadius", World::ChunkRadius);
				opaqueShader.uploadVec3("uTint", tint);
				glMultiDrawArraysIndirect(GL_TRIANGLES, NULL, solidCommandBuffer->getNumCommands(), sizeof(DrawCommand));
				solidCommandBuffer->softReset();
			}

			if (blendableCommandBuffer->getNumCommands() > 0)
			{
				const GLenum blendableDrawBuffer[3] = { GL_NONE, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
				glDrawBuffers(3, blendableDrawBuffer);

				const float zeroFillerVec[4] = { 0.0f, 0.0f, 0.0f };
				glClearBufferfv(GL_COLOR, 1, &zeroFillerVec[0]);
				const float oneFillerVec[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
				glClearBufferfv(GL_COLOR, 2, &oneFillerVec[0]);

				// Render transparent geometry
				// Disable depth writes so transparent objects don't interfere with solid passes depth values
				glDepthMask(GL_FALSE);
				glEnable(GL_BLEND);
				glBlendFunci(1, GL_ONE, GL_ONE); // Accumulation blend target
				glBlendFunci(2, GL_ZERO, GL_ONE_MINUS_SRC_COLOR); // Revealage blend target
				glBlendEquation(GL_FUNC_ADD);

				// We shouldn't need to even sort this...
				//transparentCommandBuffer().sort(playerPositionInChunkCoords);
				glBindBuffer(GL_ARRAY_BUFFER, chunkPosInstancedBuffer);
				glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(int32) * 2 * blendableCommandBuffer->getNumCommands(), blendableCommandBuffer->getChunkPosBuffer());
				glBindBuffer(GL_ARRAY_BUFFER, biomeInstancedVbo);
				glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(int32) * blendableCommandBuffer->getNumCommands(), blendableCommandBuffer->getBiomeBuffer());
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, blendableDrawCommandVbo);
				glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(DrawCommand) * blendableCommandBuffer->getNumCommands(), blendableCommandBuffer->getCommandBuffer());
				DebugStats::numDrawCalls += blendableCommandBuffer->getNumCommands();

				transparentShader.bind();
				transparentShader.uploadVec3("uPlayerPosition", playerPosition);
				transparentShader.uploadInt("uChunkRadius", World::ChunkRadius);
				transparentShader.uploadVec3("uTint", tint);

				glBindVertexArray(globalVao);
				glMultiDrawArraysIndirect(GL_TRIANGLES, NULL, blendableCommandBuffer->getNumCommands(), sizeof(DrawCommand));
				blendableCommandBuffer->softReset();

				// Reset render state
				glEnable(GL_CULL_FACE);
				glDepthMask(GL_TRUE);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

				const GLenum mainDrawBuffer[3] = { GL_COLOR_ATTACHMENT0, GL_NONE, GL_NONE };
				glDrawBuffers(3, mainDrawBuffer);

				// Blend the opaque and blended stuff together now...
				// Set the render state for compositing our transparent and opaque buffers
				glDepthFunc(GL_ALWAYS);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

				// Draw the screen quad
				Framebuffer& mainFramebuffer = Application::getMainFramebuffer();
				compositeShader.bind();
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, mainFramebuffer.getColorAttachment(1).graphicsId);
				compositeShader.uploadInt("accumulationTexture", 0);
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, mainFramebuffer.getColorAttachment(2).graphicsId);
				compositeShader.uploadInt("revealTexture", 1);

				glBindVertexArray(Vertices::fullScreenSpaceRectangleVao);
				glDrawArrays(GL_TRIANGLES, 0, 6);

				glDepthFunc(GL_LESS);
			}
		}

		void checkChunkRadius(const glm::vec3& playerPosition, bool isClient)
		{
#ifdef _USE_OPTICK
			OPTICK_EVENT();
#endif

			glm::ivec2 playerPosChunkCoords = World::toChunkCoords(playerPosition);
			chunkWorker->setPlayerPosChunkCoords(playerPosChunkCoords);
			static glm::ivec2 lastPlayerPosChunkCoords = playerPosChunkCoords;

			if (isClient)
			{
				chunkWorker->beginWork();
				return;
			}

			// Remove out of range chunks
			for (int i = 0; i < (int)subChunks->size(); i++)
			{
				if ((*subChunks)[i]->state != SubChunkState::Unloaded)
				{
					glm::ivec2 chunkPos = (*subChunks)[i]->chunkCoordinates;
					const glm::ivec2 localChunkPos = chunkPos - playerPosChunkCoords;
					bool inRangeOfPlayer =
						(localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) <=
						(World::ChunkRadius * World::ChunkRadius);
					if (!inRangeOfPlayer)
					{
						auto iter = chunks.find(chunkPos);
						if (iter != chunks.end() && iter->second.state != ChunkState::Saving)
						{
							queueSaveChunk((*subChunks)[i]->chunkCoordinates);
						}
					}
				}
			}

			// Unload any chunks that have been serialized
			for (auto iter = chunks.begin(); iter != chunks.end();)
			{
				if (iter->second.state == ChunkState::Unloading)
				{
					DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed - (float)(blockPool->poolSize() * sizeof(Block));

					blockPool->freePool(iter->second.data);
					iter = chunks.erase(iter);
				}
				else
				{
					iter++;
				}
			}

			// Load/retesselate any chunks that need to be
			bool needsWork = false;
			for (int y = playerPosChunkCoords.y - World::ChunkRadius; y <= playerPosChunkCoords.y + World::ChunkRadius; y++)
			{
				for (int x = playerPosChunkCoords.x - World::ChunkRadius; x <= playerPosChunkCoords.x + World::ChunkRadius; x++)
				{
					glm::ivec2 position(x, y);
					glm::ivec2 localPos = playerPosChunkCoords - position;
					if ((localPos.x * localPos.x) + (localPos.y * localPos.y) <= (World::ChunkRadius * World::ChunkRadius))
					{
						// We have to expand in a circle that exceeds the range of chunks in this radius,
						// so we also have to make sure that we check if the chunk is in range before we
						// try to queue it. Otherwise, we end up with infinite queues that instantly get deleted
						// which clog our threads with empty work.
						needsWork = true;
						glm::ivec2 lastLocalPos = lastPlayerPosChunkCoords - position;
						bool retesselateThisChunk = getChunk(position) != nullptr &&
							(lastLocalPos.x * lastLocalPos.x) + (lastLocalPos.y * lastLocalPos.y) >= ((World::ChunkRadius - 2) * (World::ChunkRadius - 2));
						if (retesselateThisChunk)
						{
							ChunkManager::queueRetesselateChunk(position);
						}
						else
						{
							ChunkManager::queueCreateChunk(position);
						}
					}
				}
			}

			ChunkManager::queueGenerateDecorations(playerPosChunkCoords);
			ChunkManager::queueCalculateLighting(playerPosChunkCoords);
			lastPlayerPosChunkCoords = playerPosChunkCoords;

			if (needsWork)
			{
				patchChunkPointers();
				chunkWorker->beginWork();
			}
		}

		// TODO: Simplify me!
		static void retesselateChunkBlockUpdate(const glm::ivec2& chunkCoords, const glm::vec3& worldPosition, Chunk* chunk)
		{
			FillChunkCommand cmd;
			cmd.type = CommandType::TesselateVertices;
			cmd.subChunks = subChunks;
			cmd.chunk = chunk;

			// Get any neighboring chunks that need to be updated
			int numChunksToUpdate = 1;
			Chunk* chunksToUpdate[3];
			chunksToUpdate[0] = chunk;
			glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkCoords.x * 16.0f, 0.0f, chunkCoords.y * 16.0f));
			if (localPosition.x == 0)
			{
				if (chunk->bottomNeighbor)
				{
					chunksToUpdate[numChunksToUpdate++] = chunk->bottomNeighbor;
				}
			}
			else if (localPosition.x == 15)
			{
				if (chunk->topNeighbor)
				{
					chunksToUpdate[numChunksToUpdate++] = chunk->topNeighbor;
				}
			}
			if (localPosition.z == 0)
			{
				if (chunk->leftNeighbor)
				{
					chunksToUpdate[numChunksToUpdate++] = chunk->leftNeighbor;
				}
			}
			else if (localPosition.z == 15)
			{
				if (chunk->rightNeighbor)
				{
					chunksToUpdate[numChunksToUpdate++] = chunk->rightNeighbor;
				}
			}

			// Update the sub-chunks that are about to be deleted
			for (int i = 0; i < (int)subChunks->size(); i++)
			{
				if ((*subChunks)[i]->state == SubChunkState::Uploaded)
				{
					for (int j = 0; j < numChunksToUpdate; j++)
					{
						if ((*subChunks)[i]->chunkCoordinates == chunksToUpdate[j]->chunkCoords)
						{
							(*subChunks)[i]->state = SubChunkState::RetesselateVertices;
						}
					}
				}
			}

			// Queue up all the chunks
			chunkWorker->queueCommand(cmd);
			for (int i = 1; i < numChunksToUpdate; i++)
			{
				cmd.chunk = chunksToUpdate[i];
				chunkWorker->queueCommand(cmd);
			}
			chunkWorker->beginWork();
		}
	}
}