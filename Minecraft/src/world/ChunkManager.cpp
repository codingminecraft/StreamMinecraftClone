#include "world/ChunkManager.h"
#include "world/World.h"
#include "world/BlockMap.h"
#include "core/Pool.hpp"
#include "core/File.h"
#include "utils/DebugStats.h"
#include "utils/CMath.h"
#include "renderer/Shader.h"
#include "renderer/Renderer.h"
#include "renderer/Frustum.h"

namespace Minecraft
{
	enum class CommandType : uint8
	{
		SaveBlockData = 0,
		FillBlockData = 1,
		CalculateLighting = 2,
		CalculateChunkBorderLighting = 3,
		RecalculateLighting = 4,
		TesselateVertices = 5
	};

	struct FillChunkCommand
	{
		// Must be at least ChunkWidth * ChunkDepth * ChunkHeight blocks available
		Block* blockData;
		Pool<SubChunk, World::ChunkCapacity * 16>* subChunks;
		glm::ivec2 chunkCoordinates;
		glm::ivec2 playerPosChunkCoords;
		CommandType type;
		glm::vec3 blockThatUpdated;
	};

	enum class ChunkState : uint8
	{
		None,
		Unloaded,
		Saving,
		Loading,
		Loaded
	};

	struct ChunkStateData
	{
		std::atomic<ChunkState> state;
		glm::ivec2 chunkCoords;
		Block* blockData;

		ChunkStateData(const glm::ivec2& chunkCoords)
		{
			state = ChunkState::None;
			this->chunkCoords = chunkCoords;
			blockData = nullptr;
		}

		ChunkStateData(const ChunkStateData& other)
			: state(other.state.load()), chunkCoords(other.chunkCoords), blockData(other.blockData)
		{
		}

		bool operator==(const ChunkStateData& other) const
		{
			return chunkCoords == other.chunkCoords;
		}

		bool operator!=(const ChunkStateData& other) const
		{
			return !(*this == other);
		}

		struct HashFunction
		{
			std::size_t operator()(const ChunkStateData& key) const
			{
				return std::hash<int>()(key.chunkCoords.x) ^
					std::hash<int>()(key.chunkCoords.y);
			}
		};
	};

	class CompareFillChunkCommand
	{
	public:
		// Returning true means lesser priority
		bool operator()(const FillChunkCommand& a, const FillChunkCommand& b)
		{
			if (a.type != b.type)
			{
				// Order of priorities is listed in the CommandType least to greatest
				return (uint8)a.type > (uint8)b.type;
			}

			// They are the same type of command, the chunk closer to the player has higher priority
			glm::ivec2 tmpA = a.playerPosChunkCoords - a.chunkCoordinates;
			int32 aDistanceSquared = (tmpA.x * tmpA.x) + (tmpA.y * tmpA.y);
			glm::ivec2 tmpB = b.playerPosChunkCoords - b.chunkCoordinates;
			int32 bDistanceSquared = (tmpB.x * tmpB.x) + (tmpB.y * tmpB.y);
			return aDistanceSquared > bDistanceSquared;
		}
	};

	namespace ChunkManager
	{
		class ChunkWorker
		{
		public:
			ChunkWorker(int numThreads)
				: cv(), mtx(), queueMtx(), doWork(true)
			{
				for (int i = 0; i < numThreads; i++)
				{
					workerThreads.push_back(std::thread(&ChunkWorker::threadWorker, this));
				}
			}

			void free()
			{
				{
					std::lock_guard lock(mtx);
					doWork = false;
				}
				cv.notify_all();

				for (int i = 0; i < workerThreads.size(); i++)
				{
					workerThreads[i].join();
				}
			}

			void threadWorker()
			{
				bool shouldContinue = true;
				while (shouldContinue)
				{
					if (commands.empty())
					{
						// Wait until we need to do some work
						std::unique_lock<std::mutex> lock(mtx);
						if (doWork)
						{
							cv.wait(lock, [&] { return !doWork || !commands.empty(); });
							shouldContinue = true;
						}
						else
						{
							shouldContinue = false;
						}
					}

					FillChunkCommand command;
					bool processCommand = false;
					{
						std::lock_guard<std::mutex> queueLock(queueMtx);
						if (!commands.empty())
						{
							command = commands.top();
							commands.pop();
							processCommand = true;
						}
					}

					if (processCommand)
					{
						switch (command.type)
						{
						case CommandType::FillBlockData:
						{
							if (Chunk::exists(World::chunkSavePath, command.chunkCoordinates))
							{
								Chunk::deserialize(command.blockData, World::chunkSavePath, command.chunkCoordinates);
							}
							else
							{
								Chunk::generate(command.blockData, command.chunkCoordinates, World::seedAsFloat);
							}
						}
						break;
						case CommandType::CalculateLighting:
						{
							Chunk::calculateLighting(command.blockData, command.chunkCoordinates, true);

							command.type = CommandType::CalculateChunkBorderLighting;
							queueCommand(command);
							beginWork(false);
						}
						break;
						case CommandType::CalculateChunkBorderLighting:
						{
							Chunk::calculateLighting(command.blockData, command.chunkCoordinates, false);
						}
						break;
						case CommandType::RecalculateLighting:
						{
							Chunk::calculateLightingUpdate(command.blockData, command.chunkCoordinates, command.blockThatUpdated);
						}
						break;
						case CommandType::TesselateVertices:
						{
							Chunk::generateRenderData(command.subChunks, command.blockData, command.chunkCoordinates);
						}
						break;
						case CommandType::SaveBlockData:
						{
							// Unload all sub-chunks
							for (int i = 0; i < (int)command.subChunks->size(); i++)
							{
								if ((*command.subChunks)[i]->state == SubChunkState::Uploaded && (*command.subChunks)[i]->chunkCoordinates == command.chunkCoordinates)
								{
									(*command.subChunks)[i]->state = SubChunkState::Unloaded;
									(*command.subChunks)[i]->numVertsUsed = 0;
									command.subChunks->freePool(i);
									DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed - (World::MaxVertsPerSubChunk * sizeof(Vertex));
								}
							}

							// Serialize block data
							Chunk::serialize(World::chunkSavePath, command.blockData, command.chunkCoordinates);

							// Tell the chunk manager we are done
							ChunkManager::unloadChunk(command.chunkCoordinates);
						}
						break;
						}
					}
				}
			}

			void queueCommand(FillChunkCommand& command)
			{
				command.playerPosChunkCoords = playerPosChunkCoords.load();
				{
					std::lock_guard<std::mutex> lockGuard(queueMtx);
					commands.push(command);
				}
			}

			void beginWork(bool notifyAll = true)
			{
				if (notifyAll)
				{
					cv.notify_all();
				}
				else
				{
					cv.notify_one();
				}
			}

			void setPlayerPosChunkCoords(const glm::ivec2& playerPosChunkCoords)
			{
				this->playerPosChunkCoords = playerPosChunkCoords;
			}

		private:
			std::priority_queue<FillChunkCommand, std::vector<FillChunkCommand>, CompareFillChunkCommand> commands;
			std::vector<std::thread> workerThreads;
			std::atomic<glm::ivec2> playerPosChunkCoords;
			std::condition_variable cv;
			std::mutex mtx;
			std::mutex queueMtx;
			bool doWork;
		};

		struct DrawCommand
		{
			DrawArraysIndirectCommand command;
			int distanceToPlayer;
			int level;

			bool operator<(const DrawCommand& b) const
			{
				return distanceToPlayer < b.distanceToPlayer;
			}
		};

		class CommandBufferContainer
		{
		public:
			CommandBufferContainer(int maxNumCommands)
			{
				this->maxNumCommands = maxNumCommands;
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
				// Sort chunks front to back
				std::sort(commandBuffer, commandBuffer + numCommands);
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
			DrawCommand* commandBuffer;
			int32* chunkPosBuffer;
			int32* biomeBuffer;
		};

		// Internal functions
		static void retesselateChunkBlockUpdate(const glm::ivec2& chunkCoords, const glm::vec3& worldPosition, Block* blockData);
		static const ChunkStateData* getChunkState(const glm::ivec2& chunkCoords);

		// Internal variables
		static std::mutex chunkMtx;
		static uint32 processorCount = 0;
		static std::unordered_set<ChunkStateData, ChunkStateData::HashFunction> chunkStates = {};
		static std::list<Block*> chunkFreeList = {};

		static uint32 chunkPosInstancedBuffer;
		static uint32 biomeInstancedVbo;
		static uint32 globalVao;
		static uint32 drawCommandVbo;

		// Singletons
		static ChunkWorker& chunkWorker()
		{
			static ChunkWorker instance(processorCount);
			return instance;
		}

		static Pool<SubChunk, World::ChunkCapacity * 16>& subChunks()
		{
			static Pool<SubChunk, World::ChunkCapacity * 16> instance{ 1 };
			return instance;
		}

		static Pool<Block, World::ChunkCapacity>& blockPool()
		{
			static Pool<Block, World::ChunkCapacity> instance(World::ChunkDepth * World::ChunkWidth * World::ChunkHeight);
			return instance;
		}

		static CommandBufferContainer& commandBuffer()
		{
			static CommandBufferContainer instance(subChunks().size());
			return instance;
		}

		void init()
		{
			// A chunk uses 55,000 vertices on average, so a sub-chunk can be estimated to use about 
			// 4,500 vertices on average. That's the default vertex bucket size
			processorCount = std::thread::hardware_concurrency();

			// Initialize the singletons
			chunkWorker();
			blockPool();

			// Initialize the free list
			for (int i = 0; i < (int)blockPool().size(); i++)
			{
				chunkFreeList.push_back(blockPool()[i]);
			}

			// Set up draw commands to relate to our sub chunks
			commandBuffer().init();
			glCreateBuffers(1, &drawCommandVbo);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, drawCommandVbo);
			glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(DrawCommand) * subChunks().size(), NULL, GL_DYNAMIC_DRAW);

			// Initialize the SubChunks
			chunkStates.clear();

			// Generate a bunch of empty vertex buckets for GPU use
			glCreateVertexArrays(1, &globalVao);
			glBindVertexArray(globalVao);

			uint32 renderVbo;
			glGenBuffers(1, &renderVbo);

			size_t totalSizeOfSubChunkVertices = subChunks().size() * World::MaxVertsPerSubChunk * sizeof(Vertex);
			glBindBuffer(GL_ARRAY_BUFFER, renderVbo);

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
			Vertex* basePointer = (Vertex*)glMapBufferRange(GL_ARRAY_BUFFER, 0, subChunks().size() * World::MaxVertsPerSubChunk, flags);
			for (uint32 i = 0; i < subChunks().size(); i++)
			{
				// Assign the pointers for the data on the CPU
				subChunks()[i]->first = (i * World::MaxVertsPerSubChunk);
				subChunks()[i]->data = basePointer + subChunks()[i]->first;
				subChunks()[i]->numVertsUsed = 0;
				subChunks()[i]->drawCommandIndex = i;
				subChunks()[i]->state = SubChunkState::Unloaded;
			}

			// Set up the instanced chunk pos vertex buffer
			glCreateBuffers(1, &chunkPosInstancedBuffer);
			glBindBuffer(GL_ARRAY_BUFFER, chunkPosInstancedBuffer);
			glBufferData(GL_ARRAY_BUFFER, sizeof(int32) * 2 * subChunks().size(), NULL, GL_DYNAMIC_DRAW);

			glVertexAttribIPointer(10, 2, GL_INT, sizeof(int32) * 2, 0);
			glVertexAttribDivisor(10, 1);
			glEnableVertexAttribArray(10);

			// Set up biome buffer
			glCreateBuffers(1, &biomeInstancedVbo);
			glBindBuffer(GL_ARRAY_BUFFER, biomeInstancedVbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(int32) * subChunks().size(), NULL, GL_DYNAMIC_DRAW);

			glVertexAttribIPointer(11, 1, GL_INT, sizeof(int32), 0);
			glVertexAttribDivisor(11, 1);
			glEnableVertexAttribArray(11);

			// Subchunk = 16x16x16  Blocks
			// BigChunk = 16x256x16 Blocks
			g_logger_info("Vertex Pool Total Size: %2.3f Gb", (float)(totalSizeOfSubChunkVertices / (1024.0f * 1024 * 1024)));
			g_logger_info("Block Pool Total Size: %2.3f Gb", (float)(blockPool().totalSize() / (1024.0f * 1024 * 1024)));
			DebugStats::totalChunkRamAvailable = totalSizeOfSubChunkVertices + (float)blockPool().totalSize();
		}

		void free()
		{
			chunkWorker().free();
			commandBuffer().free();
		}

		void serialize()
		{
			FillChunkCommand cmd;
			cmd.type = CommandType::SaveBlockData;
			cmd.subChunks = &(subChunks());

			for (const ChunkStateData& chunkState : chunkStates)
			{
				const glm::ivec2& chunkCoords = chunkState.chunkCoords;
				Block* blockData = chunkState.blockData;
				if (chunkState.state != ChunkState::Saving && blockData)
				{
					cmd.chunkCoordinates = chunkCoords;
					cmd.blockData = blockData;
					chunkWorker().queueCommand(cmd);
				}
			}
		}

		void queueCreateChunk(const glm::ivec2& chunkCoordinates)
		{
			// Only upload if we need to
			const ChunkStateData* chunkState = getChunkState(chunkCoordinates);
			if (!chunkState)
			{
				if (chunkFreeList.size() > 0)
				{
					FillChunkCommand cmd;
					cmd.chunkCoordinates = chunkCoordinates;
					cmd.type = CommandType::FillBlockData;
					cmd.blockData = chunkFreeList.front();
					chunkFreeList.pop_front();
					cmd.subChunks = &(subChunks());

					// Queue the fill command
					chunkWorker().queueCommand(cmd);
					// Queue the calculate lighting command
					cmd.type = CommandType::CalculateLighting;
					chunkWorker().queueCommand(cmd);
					// Queue the tesselate command
					cmd.type = CommandType::TesselateVertices;
					chunkWorker().queueCommand(cmd);

					ChunkStateData newChunk = ChunkStateData(chunkCoordinates);
					newChunk.blockData = cmd.blockData;
					newChunk.state = ChunkState::Loaded;
					DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed + blockPool().poolSize() * sizeof(Block);

					{
						std::lock_guard lock(chunkMtx);
						chunkStates.emplace(newChunk);
					}
				}
				else
				{
					// What do we do if there were no free blocks?
					g_logger_warning("No free pools for block data.");
				}
			}
		}

		void queueRecalculateLighting(const glm::ivec2& chunkCoordinates, const glm::vec3& blockPositionThatUpdated)
		{
			// Only recalculate if we need to
			Block* blockData = getChunk(chunkCoordinates);
			if (blockData)
			{
				FillChunkCommand cmd;
				cmd.chunkCoordinates = chunkCoordinates;
				cmd.type = CommandType::RecalculateLighting;
				cmd.subChunks = &subChunks();
				cmd.blockData = blockData;
				cmd.blockThatUpdated = blockPositionThatUpdated;

				chunkWorker().queueCommand(cmd);
				queueRetesselateChunk(chunkCoordinates, blockData);
			}
		}

		void queueRetesselateChunk(const glm::ivec2& chunkCoordinates, Block* blockData)
		{
			if (blockData == nullptr)
			{
				blockData = getChunk(chunkCoordinates);
			}

			if (blockData)
			{
				FillChunkCommand cmd;
				cmd.chunkCoordinates = chunkCoordinates;
				cmd.type = CommandType::TesselateVertices;
				cmd.subChunks = &subChunks();
				cmd.blockData = blockData;

				// Update the sub-chunks that are about to be deleted
				for (int i = 0; i < (int)subChunks().size(); i++)
				{
					if (subChunks()[i]->chunkCoordinates == chunkCoordinates && subChunks()[i]->state == SubChunkState::Uploaded)
					{
						subChunks()[i]->state = SubChunkState::RetesselateVertices;
					}
				}

				chunkWorker().queueCommand(cmd);
				chunkWorker().beginWork();
			}
		}

		void queueSaveChunk(const glm::ivec2& chunkCoordinates)
		{
			// TODO: This could be bad... Maybe we should return a copy of the ChunkStateData instead of a pointer
			// because it's very possible that the pointer could become invalid 

			// Only save if we need to
			const ChunkStateData* state = getChunkState(chunkCoordinates);
			if (state != nullptr)
			{
				if (state->state != ChunkState::Saving && state->blockData)
				{
					FillChunkCommand cmd;
					cmd.chunkCoordinates = chunkCoordinates;
					cmd.type = CommandType::SaveBlockData;
					cmd.blockData = state->blockData;
					cmd.subChunks = &(subChunks());
					chunkWorker().queueCommand(cmd);
				}
			}
		}

		Block getBlock(const glm::vec3& worldPosition)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			Block* blockData = getChunk(worldPosition);

			if (blockData == nullptr)
			{
				// Assume it's a chunk that's out of bounds
				// TODO: Make this only return null block if it's far far away from the player
				return BlockMap::NULL_BLOCK;
			}

			return Chunk::getBlock(worldPosition, chunkCoords, blockData);
		}

		void setBlock(const glm::vec3& worldPosition, Block newBlock)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			Block* blockData = getChunk(worldPosition);

			if (blockData == nullptr)
			{
				if (worldPosition.y >= 0 && worldPosition.y < 256)
				{
					// Assume it's a chunk that's out of bounds
					g_logger_warning("Tried to set invalid block at position<%2.3f, %2.3f, %2.3f>!", worldPosition.x, worldPosition.y, worldPosition.z);
				}
				return;
			}

			if (Chunk::setBlock(worldPosition, chunkCoords, blockData, newBlock))
			{
				retesselateChunkBlockUpdate(chunkCoords, worldPosition, blockData);
				queueRecalculateLighting(chunkCoords, worldPosition);
				chunkWorker().beginWork();
			}
		}

		void removeBlock(const glm::vec3& worldPosition)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			Block* blockData = getChunk(worldPosition);

			if (blockData == nullptr)
			{
				if (worldPosition.y >= 0 && worldPosition.y < 256)
				{
					// Assume it's a chunk that's out of bounds
					g_logger_warning("Tried to set invalid block at position<%2.3f, %2.3f, %2.3f>!", worldPosition.x, worldPosition.y, worldPosition.z);
				}
				return;
			}

			if (Chunk::removeBlock(worldPosition, chunkCoords, blockData))
			{
				retesselateChunkBlockUpdate(chunkCoords, worldPosition, blockData);
				queueRecalculateLighting(chunkCoords, worldPosition);
				chunkWorker().beginWork();
			}
		}

		Block* getChunk(const glm::vec3& worldPosition)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			return getChunk(chunkCoords);
		}

		Block* getChunk(const glm::ivec2& chunkCoords)
		{
			Block* blockData = nullptr;

			{
				std::lock_guard<std::mutex> lock(chunkMtx);
				auto iter = chunkStates.find(chunkCoords);
				if (iter != chunkStates.end())
				{
					blockData = iter->blockData;
				}
			}

			return blockData;
		}

		static const ChunkStateData* getChunkState(const glm::ivec2& chunkCoords)
		{
			const ChunkStateData* state = nullptr;

			{
				std::lock_guard<std::mutex> lock(chunkMtx);
				auto iter = chunkStates.find(chunkCoords);
				if (iter != chunkStates.end())
				{
					state = &(*iter);
				}
			}

			return state;
		}

		void render(const glm::vec3& playerPosition, const glm::ivec2& playerPositionInChunkCoords, Shader& shader, const Frustum& cameraFrustum)
		{
			chunkWorker().setPlayerPosChunkCoords(playerPositionInChunkCoords);

			// TODO: Weird that I have to re-enable that here. Try to find out why?
			glEnable(GL_CULL_FACE);

			for (int i = 0; i < (int)subChunks().size(); i++)
			{
				if (subChunks()[i]->state != SubChunkState::Unloaded)
				{
					glm::ivec2 chunkPos = subChunks()[i]->chunkCoordinates;
					auto iter = chunkStates.find(ChunkStateData(chunkPos));
					if (iter == chunkStates.end() && subChunks()[i]->state != SubChunkState::TesselatingVertices)
					{
						// If the chunk coords are no longer loaded, set this chunk as not in use anymore
						subChunks()[i]->state = SubChunkState::Unloaded;
						subChunks()[i]->numVertsUsed = 0;
						subChunks().freePool(i);
					}
					else if (iter != chunkStates.end() && iter->state == ChunkState::Loaded)
					{
						if (subChunks()[i]->state == SubChunkState::UploadVerticesToGpu)
						{
							g_logger_assert(subChunks()[i]->numVertsUsed.load() > 0, "Sub Chunk should never have tried to upload 0 verts to GPU.");
							subChunks()[i]->state = SubChunkState::Uploaded;
						}

						if (subChunks()[i]->state == SubChunkState::Uploaded || subChunks()[i]->state == SubChunkState::RetesselateVertices ||
							subChunks()[i]->state == SubChunkState::DoneRetesselating)
						{
							g_logger_assert(subChunks()[i]->numVertsUsed.load() > 0, "Sub Chunk should never have tried to upload 0 verts to GPU.");
							float yCenter = (float)subChunks()[i]->subChunkLevel * 16.0f;
							glm::vec3 chunkPos = glm::vec3(subChunks()[i]->chunkCoordinates.x * World::ChunkDepth, yCenter, subChunks()[i]->chunkCoordinates.y * World::ChunkWidth);
							if (cameraFrustum.isBoxVisible(chunkPos, chunkPos + glm::vec3(16, 16, 16)))
							{
								DrawArraysIndirectCommand drawCommand;
								g_logger_assert(subChunks()[i]->numVertsUsed.load() > 0, "Sub Chunk should never have tried to upload 0 verts to GPU.");
								drawCommand.baseInstance = 0;
								drawCommand.instanceCount = 1;
								drawCommand.count = subChunks()[i]->numVertsUsed;
								drawCommand.first = subChunks()[i]->first;
								commandBuffer().add(drawCommand, subChunks()[i]->chunkCoordinates, subChunks()[i]->subChunkLevel, playerPositionInChunkCoords, 0);
							}

							if (subChunks()[i]->state == SubChunkState::DoneRetesselating)
							{
								subChunks()[i]->numVertsUsed = 0;
								subChunks()[i]->state = SubChunkState::Unloaded;
								subChunks().freePool(i);
							}
						}
					}
				}
			}

			if (commandBuffer().getNumCommands() > 0)
			{
				commandBuffer().sort(playerPositionInChunkCoords);
				glBindBuffer(GL_ARRAY_BUFFER, chunkPosInstancedBuffer);
				glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(int32) * 2 * commandBuffer().getNumCommands(), commandBuffer().getChunkPosBuffer());
				glBindBuffer(GL_ARRAY_BUFFER, biomeInstancedVbo);
				glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(int32) * commandBuffer().getNumCommands(), commandBuffer().getBiomeBuffer());
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, drawCommandVbo);
				glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(DrawCommand) * commandBuffer().getNumCommands(), commandBuffer().getCommandBuffer());
				DebugStats::numDrawCalls += commandBuffer().getNumCommands();

				glBindVertexArray(globalVao);
				shader.uploadVec3("uPlayerPosition", playerPosition);
				shader.uploadInt("uChunkRadius", World::ChunkRadius);
				glMultiDrawArraysIndirect(GL_TRIANGLES, NULL, commandBuffer().getNumCommands(), sizeof(DrawCommand));
				commandBuffer().softReset();
			}
		}

		void unloadChunk(const glm::ivec2& chunkCoordinates)
		{
			std::lock_guard lock(chunkMtx);
			auto iter = chunkStates.find(ChunkStateData(chunkCoordinates));
			// TODO: Investigate why sometimes iter->blockData is nullptr
			// I think it might be because I wasn't locking the mutex, but double check just in case
			if (iter != chunkStates.end() && iter->state != ChunkState::Saving)
			{
				DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed - (float)(blockPool().poolSize() * sizeof(Block));

				chunkFreeList.push_back(iter->blockData);
				chunkStates.erase(iter);
			}
		}

		void checkChunkRadius(const glm::vec3& playerPosition)
		{
			glm::ivec2 playerPosChunkCoords = World::toChunkCoords(playerPosition);
			chunkWorker().setPlayerPosChunkCoords(playerPosChunkCoords);
			static glm::ivec2 lastPlayerPosChunkCoords = playerPosChunkCoords;

			// Remove out of range chunks
			for (int i = 0; i < (int)subChunks().size(); i++)
			{
				if (subChunks()[i]->state != SubChunkState::Unloaded)
				{
					glm::ivec2 chunkPos = subChunks()[i]->chunkCoordinates;
					const glm::ivec2 localChunkPos = chunkPos - playerPosChunkCoords;
					bool inRangeOfPlayer =
						(localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) <=
						(World::ChunkRadius * World::ChunkRadius);
					if (!inRangeOfPlayer)
					{
						auto iter = chunkStates.find(ChunkStateData(chunkPos));
						if (iter != chunkStates.end() && iter->state != ChunkState::Saving)
						{
							queueSaveChunk(subChunks()[i]->chunkCoordinates);
						}
					}
				}
			}

			// If the player moves to a new chunk, then retesselate any chunks on the edge of the chunk radius to 
			// fix any holes there might be
			bool retesselateEdges = false;
			if (lastPlayerPosChunkCoords != playerPosChunkCoords)
			{
				retesselateEdges = true;
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
						bool retesselateThisChunk =
							(lastLocalPos.x * lastLocalPos.x) + (lastLocalPos.y * lastLocalPos.y) >= ((World::ChunkRadius - 1) * (World::ChunkRadius - 1))
							&& retesselateEdges;
						if (!retesselateThisChunk)
						{
							ChunkManager::queueCreateChunk(position);
						}
						else
						{
							ChunkManager::queueRetesselateChunk(position);
						}
					}
				}
			}

			lastPlayerPosChunkCoords = playerPosChunkCoords;

			if (needsWork)
			{
				chunkWorker().beginWork();
			}
		}

		// TODO: Simplify me!
		static void retesselateChunkBlockUpdate(const glm::ivec2& chunkCoords, const glm::vec3& worldPosition, Block* blockData)
		{
			FillChunkCommand cmd;
			cmd.chunkCoordinates = chunkCoords;
			cmd.type = CommandType::TesselateVertices;
			cmd.subChunks = &subChunks();
			cmd.blockData = blockData;

			// Get any neighboring chunks that need to be updated
			int numChunksToUpdate = 1;
			glm::ivec2 chunksToUpdate[3];
			chunksToUpdate[0] = chunkCoords;
			glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkCoords.x * 16.0f, 0.0f, chunkCoords.y * 16.0f));
			if (localPosition.x == 0)
			{
				chunksToUpdate[numChunksToUpdate++] = glm::ivec2(chunkCoords.x - 1, chunkCoords.y);
			}
			else if (localPosition.x == 15)
			{
				chunksToUpdate[numChunksToUpdate++] = glm::ivec2(chunkCoords.x + 1, chunkCoords.y);
			}
			if (localPosition.z == 0)
			{
				chunksToUpdate[numChunksToUpdate++] = glm::ivec2(chunkCoords.x, chunkCoords.y - 1);
			}
			else if (localPosition.z == 15)
			{
				chunksToUpdate[numChunksToUpdate++] = glm::ivec2(chunkCoords.x, chunkCoords.y + 1);
			}

			// Update the sub-chunks that are about to be deleted
			for (int i = 0; i < (int)subChunks().size(); i++)
			{
				if (subChunks()[i]->state == SubChunkState::Uploaded)
				{
					for (int j = 0; j < numChunksToUpdate; j++)
					{
						if (subChunks()[i]->chunkCoordinates == chunksToUpdate[j])
						{
							subChunks()[i]->state = SubChunkState::RetesselateVertices;
						}
					}
				}
			}

			// Queue up all the chunks
			chunkWorker().queueCommand(cmd);
			for (int i = 1; i < numChunksToUpdate; i++)
			{
				Block* blockData = nullptr;

				auto iter = chunkStates.find(ChunkStateData(chunksToUpdate[i]));
				if (iter != chunkStates.end())
				{
					cmd.blockData = iter->blockData;
					cmd.chunkCoordinates = chunksToUpdate[i];
					chunkWorker().queueCommand(cmd);
				}
			}
			chunkWorker().beginWork();
		}
	}

	namespace Chunk
	{
		// Internal Enums
		enum class CUBE_FACE : uint32
		{
			LEFT = 0,
			RIGHT = 1,
			BOTTOM = 2,
			TOP = 3,
			BACK = 4,
			FRONT = 5,
			SIZE = 6
		};

		enum class UV_INDEX : uint32
		{
			TOP_RIGHT = 0,
			TOP_LEFT = 1,
			BOTTOM_LEFT = 2,
			BOTTOM_RIGHT = 3,
			SIZE
		};

		// Internal Constants
		static const int POSITION_INDEX_BITMASK = 0x1FFFF;
		static const int TEX_ID_BITMASK = 0x1FFE0000;
		static const int FACE_BITMASK = 0xE0000000;

		static const int UV_INDEX_BITMASK = 0x3;
		static const int COLOR_BLOCK_BIOME_BITMASK = 0x4;
		static const int LIGHT_LEVEL_BITMASK = 0xF8;
		static const int LIGHT_COLOR_BITMASK_R = 0x00700;
		static const int LIGHT_COLOR_BITMASK_G = 0x03800;
		static const int LIGHT_COLOR_BITMASK_B = 0x1C000;

		static const int BASE_17_DEPTH = 17;
		static const int BASE_17_WIDTH = 17;
		static const int BASE_17_HEIGHT = 289;

		// Internal functions
		static int to1DArray(int x, int y, int z);
		static Block getBlockInternal(const Block* data, int x, int y, int z, const glm::ivec2& chunkCoordinates);
		static bool setBlockInternal(Block* data, int x, int y, int z, const glm::ivec2& chunkCoordinates, Block newBlock);
		static bool removeBlockInternal(Block* data, int x, int y, int z, const glm::ivec2& chunkCoordinates);
		static std::string getFormattedFilepath(const glm::ivec2& chunkCoordinates, const std::string& worldSavePath);
		static void loadBlock(Vertex* vertexData, const glm::ivec3& vert1, const glm::ivec3& vert2, const glm::ivec3& vert3, const glm::ivec3& vert4, const TextureFormat& texture, CUBE_FACE face, bool colorFaceBasedOnBiome, int lightLevel, const glm::ivec3& lightColor);
		static void updateBlockLightLevel(Block* blockData, int localX, int localY, int localZ, const glm::ivec2& chunkCoordinates, bool internalBlocksOnly, bool zeroOut, std::unordered_set<glm::ivec2>& chunksAlreadyChecked = std::unordered_set<glm::ivec2>());

		void info()
		{
			g_logger_info("%d size of chunk", sizeof(Block) * World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);
			g_logger_info("Max %d size of vertex data", sizeof(Vertex) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth * 24);
		}

		void generate(Block* blockData, const glm::ivec2& chunkCoordinates, float seed)
		{
			const int worldChunkX = chunkCoordinates.x * 16;
			const int worldChunkZ = chunkCoordinates.y * 16;

			// TODO: Should we zero the memory in release mode as well? Or does it matter?
			g_memory_zeroMem(blockData, sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth);
			const SimplexNoise generator = SimplexNoise(World::seedAsFloat.load());
			const float scale = 0.001f;
			for (int y = 0; y < World::ChunkHeight; y++)
			{
				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						const int arrayExpansion = to1DArray(x, y, z);
						float maxHeightFloat =
							CMath::mapRange(
								generator.fractal(
									7,
									(float)(x + worldChunkX) * scale,
									(float)(z + worldChunkZ) * scale
								),
								-1.0f,
								1.0f,
								0.0f,
								1.0f
							) * 255.0f;
						int16 maxHeight = (int16)maxHeightFloat;
						int16 stoneHeight = (int16)(maxHeight - 3.0f);

						if (y == 0)
						{
							// Bedrock
							blockData[arrayExpansion].id = 7;
						}
						else if (y < stoneHeight)
						{
							// Stone
							blockData[arrayExpansion].id = 6;
						}
						else if (y < maxHeight)
						{
							// Dirt
							blockData[arrayExpansion].id = 4;
						}
						else if (y == maxHeight)
						{
							// Grass
							blockData[arrayExpansion].id = 2;
						}
						else
						{
							blockData[arrayExpansion].id = BlockMap::AIR_BLOCK.id;
						}
					}
				}
			}
		}

		void calculateLighting(Block* blockData, const glm::ivec2& chunkCoordinates, bool internalOnly)
		{
			// First set all the sky blocks and reset any non-sky blocks to 0 unless they are a source
			for (int y = World::ChunkHeight - 1; y >= 0; y--)
			{
				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						int arrayExpansion = to1DArray(x, y, z);
						if (blockData[arrayExpansion] != BlockMap::AIR_BLOCK)
						{
							continue;
						}

						// If the 32 bit is set in the block above, this is a sky block
						const Block& topNeighbor = y + 1 >= 256 ? BlockMap::NULL_BLOCK : blockData[to1DArray(x, y + 1, z)];
						int topNeighborLight = topNeighbor == BlockMap::AIR_BLOCK ? topNeighbor.lightLevel & 31 : 0;

						bool isSkyblock = (topNeighbor.lightLevel & 32) || (y == World::ChunkHeight - 1);
						if (isSkyblock)
						{
							blockData[arrayExpansion].lightLevel = 31 | 32;
						}
						else
						{
							blockData[arrayExpansion].lightLevel = blockData[arrayExpansion].lightLevel & 31;
						}

						blockData[arrayExpansion].lightColor =
							((7 << 0) & 0x7) |  // R
							((7 << 3) & 0x38) | // G
							((7 << 6) & 0x1C0); // B
					}
				}
			}

			// Then flood-fill to zero any blocks that need to be set
			for (int y = 0; y < World::ChunkHeight; y++)
			{
				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						int arrayExpansion = to1DArray(x, y, z);
						if (blockData[arrayExpansion] != BlockMap::AIR_BLOCK)
						{
							continue;
						}

						bool isSkyblock = blockData[arrayExpansion].lightLevel & 32;
						if (!isSkyblock)
						{
							// If the 32 bit is set in the block above, this is a sky block
							const Block& topNeighbor = y + 1 >= 256 ? BlockMap::NULL_BLOCK : blockData[to1DArray(x, y + 1, z)];
							int topNeighborLight = topNeighbor.isLightSource() ? topNeighbor.lightLevel & 31 : 0;
							Block bottomNeighbor = y - 1 < 0 ? BlockMap::NULL_BLOCK :
								getBlockInternal(blockData, x, y - 1, z, chunkCoordinates);
							int bottomNeighborLight = bottomNeighbor.isLightSource() ? bottomNeighbor.lightLevel & 31 : 0;
							Block leftNeighbor = internalOnly && z - 1 < 0 ? BlockMap::NULL_BLOCK :
								getBlockInternal(blockData, x, y, z - 1, chunkCoordinates);
							int leftNeighborLight = leftNeighbor.isLightSource() ? leftNeighbor.lightLevel & 31 : 0;
							Block rightNeighbor = internalOnly && z + 1 >= World::ChunkWidth ? BlockMap::NULL_BLOCK :
								getBlockInternal(blockData, x, y, z + 1, chunkCoordinates);
							int rightNeighborLight = rightNeighbor.isLightSource() ? rightNeighbor.lightLevel & 31 : 0;
							Block frontNeighbor = internalOnly && x + 1 >= World::ChunkDepth ? BlockMap::NULL_BLOCK :
								getBlockInternal(blockData, x + 1, y, z, chunkCoordinates);
							int frontNeighborLight = frontNeighbor.isLightSource() ? frontNeighbor.lightLevel & 31 : 0;
							Block backNeighbor = internalOnly && x - 1 < 0 ? BlockMap::NULL_BLOCK :
								getBlockInternal(blockData, x - 1, y, z, chunkCoordinates);
							int backNeighborLight = backNeighbor.isLightSource() ? backNeighbor.lightLevel & 31 : 0;

							// Check what the light level should be, if it's not set properly, then
							// we'll do a flood-fill from here to fill in this light properly
							int newLightLevel =
								glm::max(glm::max(leftNeighborLight,
									glm::max(rightNeighborLight,
										glm::max(topNeighborLight,
											glm::max(frontNeighborLight, backNeighborLight)
										)
									)
								) - 1, 0);
							if (newLightLevel != (blockData[arrayExpansion].lightLevel & 31))
							{
								updateBlockLightLevel(blockData, x, y, z, chunkCoordinates, internalOnly, false);
							}
						}
					}
				}
			}

			// Do a third pass, this will update all light sources and flood-fill
			//for (int y = 0; y < World::ChunkHeight; y++)
			//{
			//	for (int x = 0; x < World::ChunkDepth; x++)
			//	{
			//		for (int z = 0; z < World::ChunkWidth; z++)
			//		{
			//			int arrayExpansion = to1DArray(x, y, z);
			//			if (blockData[arrayExpansion] != BlockMap::AIR_BLOCK)
			//			{
			//				continue;
			//			}

			//			// If the 32 bit is set in the block above, this is a sky block
			//			const Block& topNeighbor = y + 1 >= 256 ? BlockMap::NULL_BLOCK : blockData[to1DArray(x, y + 1, z)];
			//			int topNeighborLight = topNeighbor == BlockMap::AIR_BLOCK ? topNeighbor.lightLevel & 31 : 0;

			//			bool isSkyblock = topNeighbor == BlockMap::AIR_BLOCK && ((topNeighbor.lightLevel & 32) || (y == World::ChunkHeight - 1));
			//			if (!isSkyblock)
			//			{
			//				// If the 32 bit is set in the block above, this is a sky block
			//				Block bottomNeighbor = y - 1 < 0 ? BlockMap::NULL_BLOCK :
			//					getBlockInternal(blockData, x, y - 1, z, chunkCoordinates);
			//				int bottomNeighborLight = bottomNeighbor == BlockMap::AIR_BLOCK ? bottomNeighbor.lightLevel & 31 : 0;
			//				Block leftNeighbor = internalOnly && z - 1 < 0 ? BlockMap::NULL_BLOCK :
			//					getBlockInternal(blockData, x, y, z - 1, chunkCoordinates);
			//				int leftNeighborLight = leftNeighbor == BlockMap::AIR_BLOCK ? leftNeighbor.lightLevel & 31 : 0;
			//				Block rightNeighbor = internalOnly && z + 1 >= World::ChunkWidth ? BlockMap::NULL_BLOCK :
			//					getBlockInternal(blockData, x, y, z + 1, chunkCoordinates);
			//				int rightNeighborLight = rightNeighbor == BlockMap::AIR_BLOCK ? rightNeighbor.lightLevel & 31 : 0;
			//				Block frontNeighbor = internalOnly && x + 1 >= World::ChunkDepth ? BlockMap::NULL_BLOCK :
			//					getBlockInternal(blockData, x + 1, y, z, chunkCoordinates);
			//				int frontNeighborLight = frontNeighbor == BlockMap::AIR_BLOCK ? frontNeighbor.lightLevel & 31 : 0;
			//				Block backNeighbor = internalOnly && x - 1 < 0 ? BlockMap::NULL_BLOCK :
			//					getBlockInternal(blockData, x - 1, y, z, chunkCoordinates);
			//				int backNeighborLight = backNeighbor == BlockMap::AIR_BLOCK ? backNeighbor.lightLevel & 31 : 0;

			//				// Check what the light level should be, if it's not set properly, then
			//				// we'll do a flood-fill from here to fill in this light properly
			//				int newLightLevel =
			//					glm::max(glm::max(leftNeighborLight,
			//						glm::max(rightNeighborLight,
			//							glm::max(topNeighborLight,
			//								glm::max(frontNeighborLight, backNeighborLight)
			//							)
			//						)
			//					) - 1, 0);
			//				if (newLightLevel != (blockData[arrayExpansion].lightLevel & 31))
			//				{
			//					glm::vec3 worldPos = glm::vec3(chunkCoordinates.x * 16.0f + x, y, chunkCoordinates.y * 16.0f + z);
			//					//updateBlockLightLevel(blockData, x, y, z, chunkCoordinates, internalOnly, false);
			//				}
			//			}
			//		}
			//	}
			//}
		}

		static glm::ivec3 offsetsToAdd[6] = {
			{1, 0, 0},
			{-1, 0, 0},
			{0, 1, 0},
			{0, -1, 0},
			{0, 0, 1},
			{0, 0, -1}
		};
		void calculateLightingUpdate(Block* blockData, const glm::ivec2& chunkCoordinates, const glm::vec3& blockPosition)
		{
			glm::ivec3 localPosition = glm::floor(blockPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			int localX = localPosition.x;
			int localY = localPosition.y;
			int localZ = localPosition.z;
			Block blockThatsUpdating = blockData[to1DArray(localX, localY, localZ)];
			if (!blockThatsUpdating.isLightSource())
			{
				bool offsetsAdded[6];
				// First update all blocks surrounding this block that are inside this chunk
				for (int i = 0; i < 6; i++)
				{
					glm::ivec3 blockToCheck = localPosition + offsetsToAdd[i];
					if (blockToCheck.x >= World::ChunkDepth || blockToCheck.x < 0 || blockToCheck.y >= World::ChunkHeight || blockToCheck.y < 0
						|| blockToCheck.z >= World::ChunkWidth || blockToCheck.z < 0)
					{
						offsetsAdded[i] = false;
					}
					else
					{
						offsetsAdded[i] = true;
						updateBlockLightLevel(blockData, blockToCheck.x, blockToCheck.y, blockToCheck.z, chunkCoordinates, false, true);
					}
				}

				// Then update any blocks that were outside this chunk and surrounding the block just placed
				for (int i = 0; i < 6; i++)
				{
					if (!offsetsAdded[i])
					{
						glm::vec3 blockToCheckWorldPos = blockPosition + glm::vec3(offsetsToAdd[i]);
						glm::ivec2 blockToCheckChunkPos = World::toChunkCoords(blockToCheckWorldPos);
						glm::ivec3 localPosition = glm::floor(blockToCheckWorldPos - glm::vec3(blockToCheckChunkPos.x * 16.0f, 0.0f, blockToCheckChunkPos.y * 16.0f));
						blockData = ChunkManager::getChunk(blockToCheckChunkPos);
						updateBlockLightLevel(blockData, localPosition.x, localPosition.y, localPosition.z, chunkCoordinates, false, true);
					}
				}
			}
			else
			{
				updateBlockLightLevel(blockData, localX, localY, localZ, chunkCoordinates, false, false);
			}
		}

		Block getLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, const Block* blockData)
		{
			return getBlockInternal(blockData, localPosition.x, localPosition.y, localPosition.z, chunkCoordinates);
		}

		Block getBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, const Block* blockData)
		{
			glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			return getLocalBlock(localPosition, chunkCoordinates, blockData);
		}

		bool setLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, Block* blockData, Block newBlock)
		{
			return setBlockInternal(blockData, localPosition.x, localPosition.y, localPosition.z, chunkCoordinates, newBlock);
		}

		bool setBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, Block* blockData, Block newBlock)
		{
			glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			return setLocalBlock(localPosition, chunkCoordinates, blockData, newBlock);
		}

		bool removeLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, Block* blockData)
		{
			return removeBlockInternal(blockData, localPosition.x, localPosition.y, localPosition.z, chunkCoordinates);
		}

		bool removeBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, Block* blockData)
		{
			glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			return removeLocalBlock(localPosition, chunkCoordinates, blockData);
		}

		static SubChunk* getSubChunk(Pool<SubChunk, World::ChunkCapacity * 16>* subChunks, SubChunk* currentSubChunk, int currentLevel, const glm::ivec2& chunkCoordinates)
		{
			bool needsNewChunk = currentSubChunk == nullptr
				|| currentSubChunk->subChunkLevel != currentLevel
				|| currentSubChunk->numVertsUsed + 6 >= World::MaxVertsPerSubChunk;

			if (needsNewChunk && currentSubChunk)
			{
				currentSubChunk->state = SubChunkState::UploadVerticesToGpu;
			}

			SubChunk* ret = currentSubChunk;
			if (needsNewChunk)
			{
				if (!subChunks->empty())
				{
					DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed + (World::MaxVertsPerSubChunk * sizeof(Vertex));

					ret = subChunks->getNewPool();
					ret->state = SubChunkState::TesselatingVertices;
					ret->subChunkLevel = currentLevel;
					ret->chunkCoordinates = chunkCoordinates;
				}
				else
				{
					g_logger_warning("Ran out of sub-chunk vertex room.");
					ret = nullptr;
				}
			}
			return ret;
		}

		void generateRenderData(Pool<SubChunk, World::ChunkCapacity * 16>* subChunks, const Block* blockData, const glm::ivec2& chunkCoordinates)
		{
			const int worldChunkX = chunkCoordinates.x * 16;
			const int worldChunkZ = chunkCoordinates.y * 16;

			SubChunk* currentSubChunk = nullptr;
			for (int y = 0; y < World::ChunkHeight; y++)
			{
				int currentLevel = y / 16;

				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						// 24 Vertices per cube
						const Block& block = getBlockInternal(blockData, x, y, z, chunkCoordinates);
						int blockId = block.id;

						if (block == BlockMap::NULL_BLOCK || block == BlockMap::AIR_BLOCK)
						{
							continue;
						}

						const BlockFormat& blockFormat = BlockMap::getBlock(blockId);

						glm::ivec3 verts[8];
						verts[0] = glm::ivec3(
							x,
							y,
							z
						);
						verts[1] = verts[0] + glm::ivec3(0, 0, 1);
						verts[2] = verts[1] + glm::ivec3(1, 0, 0);
						verts[3] = verts[0] + glm::ivec3(1, 0, 0);

						verts[4] = verts[0] + glm::ivec3(0, 1, 0);
						verts[5] = verts[1] + glm::ivec3(0, 1, 0);
						verts[6] = verts[2] + glm::ivec3(0, 1, 0);
						verts[7] = verts[3] + glm::ivec3(0, 1, 0);

						// The order of coordinates is LEFT, RIGHT, BOTTOM, TOP, BACK, FRONT blocks to check
						int xCoords[6] = { x, x, x, x, x - 1, x + 1 };
						int yCoords[6] = { y, y, y - 1, y + 1, y, y };
						int zCoords[6] = { z - 1, z + 1, z, z, z, z };

						Block blocks[6];
						glm::ivec3 lightColors[6];
						int lightLevels[6];
						const TextureFormat* textures[6] = {
							&BlockMap::getTextureFormat(blockFormat.sideTexture),
							&BlockMap::getTextureFormat(blockFormat.sideTexture),
							&BlockMap::getTextureFormat(blockFormat.bottomTexture),
							&BlockMap::getTextureFormat(blockFormat.topTexture),
							&BlockMap::getTextureFormat(blockFormat.sideTexture),
							&BlockMap::getTextureFormat(blockFormat.sideTexture)
						};
						static const glm::ivec4 vertIndices[6] = {
							{0, 4, 7, 3}, // LEFT
							{2, 6, 5, 1}, // RIGHT
							{0, 3, 2, 1}, // BOTTOM
							{5, 6, 7, 4}, // TOP
							{0, 1, 5, 4}, // BACK
							{7, 6, 2, 3}  // FRONT
						};
						for (int i = 0; i < 6; i++)
						{
							blocks[i] = getBlockInternal(blockData, xCoords[i], yCoords[i], zCoords[i], chunkCoordinates);
							lightColors[i] = glm::ivec3(
								((blocks[i].lightColor & 0x7) >> 0),  // R
								((blocks[i].lightColor & 0x38) >> 3), // G
								((blocks[i].lightColor & 0x1C0) >> 6) // B;
							);
							lightLevels[i] = blocks[i] == BlockMap::AIR_BLOCK ? blocks[i].lightLevel & 31 : 0;
						}
						const BlockFormat* blockFormats[6] = {
							&BlockMap::getBlock(blocks[0].id),
							&BlockMap::getBlock(blocks[1].id),
							&BlockMap::getBlock(blocks[2].id),
							&BlockMap::getBlock(blocks[3].id),
							&BlockMap::getBlock(blocks[4].id),
							&BlockMap::getBlock(blocks[5].id)
						};

						// Only add the faces that are not culled by other blocks
						for (int i = 0; i < 6; i++)
						{
							if (blocks[i].id && blockFormats[i]->isTransparent)
							{
								currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
								if (!currentSubChunk)
								{
									// TODO: Handle running out of memory better than this
									break;
								}
								bool colorByBiome = i == (int)CUBE_FACE::TOP
									? blockFormat.colorTopByBiome
									: i == (int)CUBE_FACE::BOTTOM
									? blockFormat.colorBottomByBiome
									: blockFormat.colorSideByBiome;
								loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed,
									verts[vertIndices[i][0]],
									verts[vertIndices[i][1]],
									verts[vertIndices[i][2]],
									verts[vertIndices[i][3]],
									*textures[i],
									(CUBE_FACE)i,
									colorByBiome,
									lightLevels[i],
									lightColors[i]);
								currentSubChunk->numVertsUsed += 6;
							}
						}
					}
				}
			}

			if (currentSubChunk && currentSubChunk->numVertsUsed > 0)
			{
				currentSubChunk->state = SubChunkState::UploadVerticesToGpu;
			}

			for (int i = 0; i < (int)(*subChunks).size(); i++)
			{
				if ((*subChunks)[i]->chunkCoordinates == chunkCoordinates && (*subChunks)[i]->state == SubChunkState::RetesselateVertices)
				{
					SubChunk* subChunkToUnload = (*subChunks)[i];
					subChunkToUnload->state = SubChunkState::DoneRetesselating;
				}
			}
		}

		void serialize(const std::string& worldSavePath, const Block* blockData, const glm::ivec2& chunkCoordinates)
		{
			std::string filepath = getFormattedFilepath(chunkCoordinates, worldSavePath);
			FILE* fp = fopen(filepath.c_str(), "wb");
			fwrite(blockData, sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth, 1, fp);
			fclose(fp);
		}

		void deserialize(Block* blockData, const std::string& worldSavePath, const glm::ivec2& chunkCoordinates)
		{
			std::string filepath = getFormattedFilepath(chunkCoordinates, worldSavePath);
			FILE* fp = fopen(filepath.c_str(), "rb");
			if (!fp)
			{
				g_logger_error("Could not open file '%s'", filepath.c_str());
				return;
			}

			fread(blockData, sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth, 1, fp);
			for (int y = 0; y < World::ChunkHeight; y++)
			{
				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						blockData[to1DArray(x, y, z)].lightLevel = 0;
					}
				}
			}

			fclose(fp);
		}

		bool exists(const std::string& worldSavePath, const glm::ivec2& chunkCoordinates)
		{
			std::string filepath = getFormattedFilepath(chunkCoordinates, worldSavePath);
			return File::isFile(filepath.c_str());
		}

		// =====================================================
		// Internal functions 
		// =====================================================
		static int to1DArray(int x, int y, int z)
		{
			return (x * World::ChunkDepth) + (y * World::ChunkHeight) + z;
		}

		static Block getBlockInternal(const Block* data, int x, int y, int z, const glm::ivec2& chunkCoordinates)
		{
			int index = to1DArray(x, y, z);
			return x >= World::ChunkDepth || x < 0 || z >= World::ChunkWidth || z < 0
				? ChunkManager::getBlock(glm::vec3(chunkCoordinates.x * 16.0f + x, y, chunkCoordinates.y * 16.0f + z))
				: y >= 256 || y < 0
				? BlockMap::NULL_BLOCK
				: data[index];
		}

		extern bool stepOnce = false;
		extern bool doStepLogic = false;
		static void updateBlockLightLevel(Block* blockData, int localX, int localY, int localZ, const glm::ivec2& chunkCoordinates, bool internalBlocksOnly, bool zeroOut, std::unordered_set<glm::ivec2>& chunksAlreadyChecked)
		{
			if (localX >= 16 || localX < 0 || localZ >= 16 || localZ < 0)
			{
				g_logger_warning("Invalid local pos.");
				return;
			}

			if (localY >= 256 || localY < 0)
			{
				return;
			}

			if (!blockData[to1DArray(localX, localY, localZ)].isLightSource())
			{
				return;
			}

			glm::ivec3 backPropagateBlock = glm::ivec3(INT32_MIN, INT32_MIN, INT32_MIN);
			auto chunksToUpdate = std::unordered_map<glm::ivec2, glm::vec3>();
			auto blocksAlreadyChecked = std::unordered_set<glm::ivec3>();
			auto blocksToUpdate = std::queue<glm::ivec3>();
			blocksToUpdate.push(glm::ivec3(localX, localY, localZ));
			while (!blocksToUpdate.empty())
			{
				glm::ivec3 blockToUpdate = blocksToUpdate.front();
				blocksToUpdate.pop();
				blocksAlreadyChecked.insert(blockToUpdate);
				int x = blockToUpdate.x;
				int y = blockToUpdate.y;
				int z = blockToUpdate.z;

				if (y >= 256 || y < 0)
				{
					continue;
				}

				if (x < 0 || x >= 16 || z < 0 || z >= 16)
				{
					glm::vec3 worldPosition = glm::vec3(chunkCoordinates.x * 16.0f + x, y, chunkCoordinates.y * 16.0f + z);
					glm::ivec2 chunkToUpdateCoords = World::toChunkCoords(worldPosition);
					if (chunksAlreadyChecked.find(chunkToUpdateCoords) == chunksAlreadyChecked.end() &&
						chunksToUpdate.find(chunkToUpdateCoords) == chunksToUpdate.end())
					{
						chunksToUpdate[chunkToUpdateCoords] = worldPosition;
					}
					continue;
				}

				int arrayExpansion = to1DArray(x, y, z);
				if (!blockData[arrayExpansion].isLightSource())
				{
					continue;
				}

				// If the 32 bit is set in the block above, this is a sky block
				Block topNeighbor = y + 1 >= World::ChunkHeight ? BlockMap::NULL_BLOCK :
					getBlockInternal(blockData, x, y + 1, z, chunkCoordinates);
				int topNeighborLight = topNeighbor.isLightSource() ? topNeighbor.lightLevel & 31 : 0;
				Block bottomNeighbor = y - 1 < 0 ? BlockMap::NULL_BLOCK :
					getBlockInternal(blockData, x, y - 1, z, chunkCoordinates);
				int bottomNeighborLight = bottomNeighbor.isLightSource() ? bottomNeighbor.lightLevel & 31 : 0;
				Block leftNeighbor = internalBlocksOnly && z - 1 < 0 ? BlockMap::NULL_BLOCK :
					getBlockInternal(blockData, x, y, z - 1, chunkCoordinates);
				int leftNeighborLight = leftNeighbor.isLightSource() ? leftNeighbor.lightLevel & 31 : 0;
				Block rightNeighbor = internalBlocksOnly && z + 1 >= World::ChunkWidth ? BlockMap::NULL_BLOCK :
					getBlockInternal(blockData, x, y, z + 1, chunkCoordinates);
				int rightNeighborLight = rightNeighbor.isLightSource() ? rightNeighbor.lightLevel & 31 : 0;
				Block frontNeighbor = internalBlocksOnly && x + 1 >= World::ChunkDepth ? BlockMap::NULL_BLOCK :
					getBlockInternal(blockData, x + 1, y, z, chunkCoordinates);
				int frontNeighborLight = frontNeighbor.isLightSource() ? frontNeighbor.lightLevel & 31 : 0;
				Block backNeighbor = internalBlocksOnly && x - 1 < 0 ? BlockMap::NULL_BLOCK :
					getBlockInternal(blockData, x - 1, y, z, chunkCoordinates);
				int backNeighborLight = backNeighbor.isLightSource() ? backNeighbor.lightLevel & 31 : 0;

				// We don't have to worry about sky blocks, because those have been set properly already
				// Check if the new light level is correct
				bool thisLightLevelChanged = false;
				int newLightLevel =
					glm::max(glm::max(leftNeighborLight,
						glm::max(rightNeighborLight,
							glm::max(topNeighborLight,
								glm::max(bottomNeighborLight,
									glm::max(frontNeighborLight, backNeighborLight)
								)
							)
						)
					) - 1, 0);
				// If the top neighbor is a skyblock, set this to a skyblock too
				if (topNeighbor.lightLevel & 32)
				{
					newLightLevel = 31 | 32;
				}
				// If this block is a light source and it's not air, use the block format's base light source level for it
				if (blockData[arrayExpansion] != BlockMap::AIR_BLOCK && blockData[arrayExpansion].isLightSource())
				{
					newLightLevel = BlockMap::getBlock(blockData[arrayExpansion].id).lightLevel;
				}

				if (blockData[arrayExpansion].lightLevel != newLightLevel)
				{
					// We need to update our light level immediately if it's supposed to be a skyblock
					if (!zeroOut || (newLightLevel & 32))
					{
						blockData[arrayExpansion].lightLevel = newLightLevel;
					}
					thisLightLevelChanged = true;
				}
				else if (blockData[arrayExpansion].lightLevel == newLightLevel && newLightLevel != 0 && zeroOut)
				{
					// Mark the correct block to start propagating backwards from
					backPropagateBlock = glm::ivec3(x, y, z);
				}

				if (thisLightLevelChanged)
				{
					blockData[arrayExpansion].lightColor =
						((7 << 0) & 0x7) | // R
						((7 << 3) & 0x38) | // G
						((7 << 6) & 0x1C0); // B
					if ((blockData[arrayExpansion].lightLevel & 31) != 0)
					{
						// We skip a block if it's a skylight or if we've already checked it because it won't change in
						// either case
						if ((blocksAlreadyChecked.find(glm::ivec3(x, y + 1, z)) == blocksAlreadyChecked.end() &&
							topNeighbor == BlockMap::AIR_BLOCK))
						{
							blocksToUpdate.push(glm::ivec3(x, y + 1, z));
						}
						if ((blocksAlreadyChecked.find(glm::ivec3(x, y - 1, z)) == blocksAlreadyChecked.end() &&
							bottomNeighbor == BlockMap::AIR_BLOCK))
						{
							blocksToUpdate.push(glm::ivec3(x, y - 1, z));
						}
						if ((blocksAlreadyChecked.find(glm::ivec3(x, y, z - 1)) == blocksAlreadyChecked.end() &&
							leftNeighbor == BlockMap::AIR_BLOCK))
						{
							blocksToUpdate.push(glm::ivec3(x, y, z - 1));
						}
						if ((blocksAlreadyChecked.find(glm::ivec3(x, y, z + 1)) == blocksAlreadyChecked.end() &&
							rightNeighbor == BlockMap::AIR_BLOCK))
						{
							blocksToUpdate.push(glm::ivec3(x, y, z + 1));
						}
						if ((blocksAlreadyChecked.find(glm::ivec3(x + 1, y, z)) == blocksAlreadyChecked.end() &&
							frontNeighbor == BlockMap::AIR_BLOCK))
						{
							blocksToUpdate.push(glm::ivec3(x + 1, y, z));
						}
						if ((blocksAlreadyChecked.find(glm::ivec3(x - 1, y, z)) == blocksAlreadyChecked.end() &&
							backNeighbor == BlockMap::AIR_BLOCK))
						{
							blocksToUpdate.push(glm::ivec3(x - 1, y, z));
						}
					}

					if (zeroOut)
					{
						blockData[arrayExpansion].lightLevel = 0;
					}

					if (doStepLogic && blockData[arrayExpansion] == BlockMap::AIR_BLOCK)
					{
						while (!stepOnce && doStepLogic)
						{
							std::this_thread::sleep_for(std::chrono::milliseconds(16));
						}
						ChunkManager::queueRetesselateChunk(chunkCoordinates, blockData);
						stepOnce = false;
					}
				}
			}

			for (auto& chunk : chunksToUpdate)
			{
				Block* chunkBlockData = ChunkManager::getChunk(chunk.first);
				chunksAlreadyChecked.insert(chunk.first);
				if (chunkBlockData)
				{
					glm::ivec3 localPosition = glm::floor(chunk.second - glm::vec3(chunk.first.x * 16.0f, 0.0f, chunk.first.y * 16.0f));
					updateBlockLightLevel(chunkBlockData, localPosition.x, localPosition.y, localPosition.z, chunk.first, false, zeroOut, chunksAlreadyChecked);
					ChunkManager::queueRetesselateChunk(chunk.first, chunkBlockData);
				}
			}

			if (backPropagateBlock != glm::ivec3(INT32_MIN, INT32_MIN, INT32_MIN))
			{
				glm::vec3 worldPosition = glm::vec3(chunkCoordinates.x * 16.0f + backPropagateBlock.x, backPropagateBlock.y,
					chunkCoordinates.y * 16.0f + backPropagateBlock.z);
				glm::ivec2 chunkToUpdateCoords = World::toChunkCoords(worldPosition);
				glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkToUpdateCoords.x * 16.0f, 0.0f, chunkToUpdateCoords.y * 16.0f));
				Block* chunkBlockData = ChunkManager::getChunk(chunkToUpdateCoords);
				chunkBlockData[to1DArray(localPosition.x, localPosition.y, localPosition.z)].lightLevel = 0;
				updateBlockLightLevel(chunkBlockData, localPosition.x, localPosition.y, localPosition.z, chunkToUpdateCoords, false, false);
			}
		}

		static bool setBlockInternal(Block* data, int x, int y, int z, const glm::ivec2& chunkCoordinates, Block newBlock)
		{
			int index = to1DArray(x, y, z);
			if (x >= 16 || x < 0 || z >= 16 || z < 0)
			{
				g_logger_warning("Tried to set internal block in the wrong chunk.");
				return false;
			}
			else if (y >= 256 || y < 0)
			{
				g_logger_warning("Tried to set invalid block position, y >= 256 || y < 0.");
				return false;
			}

			data[index] = newBlock;
			data[index].lightLevel = 0;// BlockMap::getBlock(newBlock.id).lightLevel;

			return true;
		}

		static bool removeBlockInternal(Block* data, int x, int y, int z, const glm::ivec2& chunkCoordinates)
		{
			int index = to1DArray(x, y, z);
			if (x >= 16 || x < 0 || z >= 16 || z < 0)
			{
				g_logger_warning("Tried to remove internal block in the wrong chunk.");
				return false;
			}
			else if (y >= 256 || y < 0)
			{
				g_logger_warning("Tried to remove invalid block position, y >= 256 || y < 0.");
				return false;
			}

			//bool wasLightSource = data[index].isLightSource();
			//int oldLightLevel = data[index].lightLevel;
			data[index] = BlockMap::AIR_BLOCK;
			data[index].lightColor =
				((7 << 0) & 0x7) | // R
				((7 << 3) & 0x38) | // G
				((7 << 6) & 0x1C0); // B
			//if (wasLightSource)
			//{
			//	data[index].lightLevel = oldLightLevel;
			//}

			return true;
		}

		static std::string getFormattedFilepath(const glm::ivec2& chunkCoordinates, const std::string& worldSavePath)
		{
			return worldSavePath + "/" + std::to_string(chunkCoordinates.x) + "_" + std::to_string(chunkCoordinates.y) + ".bin";
		}

		static int toCompressedVec3(int x, int y, int z)
		{
			return (x * BASE_17_DEPTH) + (y * BASE_17_HEIGHT) + z;
		}

		static glm::ivec3 toCoordinates(int index)
		{
			const int z = index % BASE_17_WIDTH;
			const int x = (index % BASE_17_HEIGHT) / BASE_17_DEPTH;
			const int y = (index - (x * BASE_17_DEPTH) - z) / BASE_17_HEIGHT;
			return {
				x, y, z
			};
		}

		static Vertex compress(
			const glm::ivec3& vertex,
			const TextureFormat& texture,
			CUBE_FACE face,
			UV_INDEX uvIndex,
			bool colorVertexBasedOnBiome,
			int lightLevel,
			const glm::ivec3& lightColor)
		{
			// Bits  0-16 position index
			// Bits 17-28 texId
			// Bits 29-31 normalDir face value
			uint32 data1 = 0;

			int positionIndex = toCompressedVec3(vertex.x, vertex.y, vertex.z);
			data1 |= ((positionIndex << 0) & POSITION_INDEX_BITMASK);
			data1 |= ((texture.id << 17) & TEX_ID_BITMASK);
			data1 |= ((uint32)face << 29) & FACE_BITMASK;

			uint32 data2 = 0;

			// Bits 0-1  UV Index -- this tells us which corner to use for the texture coords
			// Bit  2    Color the block based on biome
			// Bits 4-8  Light level
			// Bits 9-17 Light color
			data2 |= (((uint32)uvIndex << 0) & UV_INDEX_BITMASK);
			data2 |= (((uint32)(colorVertexBasedOnBiome ? 1 : 0) << 2) & COLOR_BLOCK_BIOME_BITMASK);
			data2 |= (((uint32)(lightLevel << 3) & LIGHT_LEVEL_BITMASK));
			data2 |= (((uint32)(lightColor.r << 8) & LIGHT_COLOR_BITMASK_R));
			data2 |= (((uint32)(lightColor.g << 11) & LIGHT_COLOR_BITMASK_G));
			data2 |= (((uint32)(lightColor.b << 14) & LIGHT_COLOR_BITMASK_B));

			return {
				data1,
				data2
			};
		}

		static void loadBlock(
			Vertex* vertexData,
			const glm::ivec3& vert1,
			const glm::ivec3& vert2,
			const glm::ivec3& vert3,
			const glm::ivec3& vert4,
			const TextureFormat& texture,
			CUBE_FACE face,
			bool colorFaceBasedOnBiome,
			int lightLevel,
			const glm::ivec3& lightColor)
		{
			UV_INDEX uv0 = UV_INDEX::BOTTOM_RIGHT;
			UV_INDEX uv1 = UV_INDEX::TOP_RIGHT;
			UV_INDEX uv2 = UV_INDEX::TOP_LEFT;

			UV_INDEX uv3 = UV_INDEX::BOTTOM_RIGHT;
			UV_INDEX uv4 = UV_INDEX::TOP_LEFT;
			UV_INDEX uv5 = UV_INDEX::BOTTOM_LEFT;

			switch (face)
			{
			case CUBE_FACE::BACK:
				uv0 = (UV_INDEX)(((int)uv0 + 2) % (int)UV_INDEX::SIZE);
				uv1 = (UV_INDEX)(((int)uv1 + 2) % (int)UV_INDEX::SIZE);
				uv2 = (UV_INDEX)(((int)uv2 + 2) % (int)UV_INDEX::SIZE);
				uv3 = (UV_INDEX)(((int)uv3 + 2) % (int)UV_INDEX::SIZE);
				uv4 = (UV_INDEX)(((int)uv4 + 2) % (int)UV_INDEX::SIZE);
				uv5 = (UV_INDEX)(((int)uv5 + 2) % (int)UV_INDEX::SIZE);
				break;
			case CUBE_FACE::RIGHT:
				uv0 = (UV_INDEX)(((int)uv0 + 3) % (int)UV_INDEX::SIZE);
				uv1 = (UV_INDEX)(((int)uv1 + 3) % (int)UV_INDEX::SIZE);
				uv2 = (UV_INDEX)(((int)uv2 + 3) % (int)UV_INDEX::SIZE);
				uv3 = (UV_INDEX)(((int)uv3 + 3) % (int)UV_INDEX::SIZE);
				uv4 = (UV_INDEX)(((int)uv4 + 3) % (int)UV_INDEX::SIZE);
				uv5 = (UV_INDEX)(((int)uv5 + 3) % (int)UV_INDEX::SIZE);
				break;
			case CUBE_FACE::LEFT:
				uv0 = (UV_INDEX)(((int)uv0 + 3) % (int)UV_INDEX::SIZE);
				uv1 = (UV_INDEX)(((int)uv1 + 3) % (int)UV_INDEX::SIZE);
				uv2 = (UV_INDEX)(((int)uv2 + 3) % (int)UV_INDEX::SIZE);
				uv3 = (UV_INDEX)(((int)uv3 + 3) % (int)UV_INDEX::SIZE);
				uv4 = (UV_INDEX)(((int)uv4 + 3) % (int)UV_INDEX::SIZE);
				uv5 = (UV_INDEX)(((int)uv5 + 3) % (int)UV_INDEX::SIZE);
				break;
			}

			vertexData[0] = compress(vert1, texture, face, uv0, colorFaceBasedOnBiome, lightLevel, lightColor);
			vertexData[1] = compress(vert2, texture, face, uv1, colorFaceBasedOnBiome, lightLevel, lightColor);
			vertexData[2] = compress(vert3, texture, face, uv2, colorFaceBasedOnBiome, lightLevel, lightColor);

			vertexData[3] = compress(vert1, texture, face, uv3, colorFaceBasedOnBiome, lightLevel, lightColor);
			vertexData[4] = compress(vert3, texture, face, uv4, colorFaceBasedOnBiome, lightLevel, lightColor);
			vertexData[5] = compress(vert4, texture, face, uv5, colorFaceBasedOnBiome, lightLevel, lightColor);
		}
	}
}