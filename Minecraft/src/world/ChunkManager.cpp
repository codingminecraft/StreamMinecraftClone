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
		FillBlockData,
		TesselateVertices,
		SaveBlockData
	};

	struct FillChunkCommand
	{
		// Must be at least ChunkWidth * ChunkDepth * ChunkHeight blocks available
		Block* blockData;
		Pool<SubChunk, World::ChunkCapacity * 16>* subChunks;
		glm::ivec2 chunkCoordinates;
		glm::ivec2 playerPosChunkCoords;
		CommandType type;
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
				// Order of priorities
				// 1. Save Block Data 
				// 2. Fill Block Data
				// 3. Tesselate
				return a.type == CommandType::SaveBlockData
					? false
					: a.type == CommandType::FillBlockData
					? false
					: true;
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

							// Queue the tesselation commands
							command.type = CommandType::TesselateVertices;
							queueCommand(command);
							beginWork(false);
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
							for (int i = 0; i < command.subChunks->size(); i++)
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

		// Internal variables
		static std::mutex chunkMtx;
		static uint32 processorCount = 0;
		static std::unordered_set<ChunkStateData, ChunkStateData::HashFunction> chunkStates = {};
		static std::list<Block*> chunkFreeList = {};
		static void retesselateChunkBlockUpdate(const glm::ivec2& chunkCoords, const glm::vec3& worldPosition, Block* blockData);

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
			for (int i = 0; i < blockPool().size(); i++)
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
			DebugStats::totalChunkRamAvailable = totalSizeOfSubChunkVertices + blockPool().totalSize();
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

		void queueCreateChunk(const glm::ivec2& chunkCoordinates, bool retesselate)
		{
			// Only upload if we need to
			bool upload;
			{
				std::lock_guard<std::mutex> lock(chunkMtx);
				auto iter = chunkStates.find(ChunkStateData(chunkCoordinates));
				upload = iter == chunkStates.end();
			}
			if (upload)
			{
				if (chunkFreeList.size() > 0)
				{
					FillChunkCommand cmd;
					cmd.chunkCoordinates = chunkCoordinates;
					cmd.type = CommandType::FillBlockData;
					cmd.blockData = chunkFreeList.front();
					chunkFreeList.pop_front();
					cmd.subChunks = &(subChunks());
					chunkWorker().queueCommand(cmd);

					ChunkStateData newChunk = ChunkStateData(chunkCoordinates);
					newChunk.blockData = cmd.blockData;
					newChunk.state = ChunkState::Loaded;
					DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed + blockPool().poolSize() * sizeof(Block);

					std::lock_guard lock(chunkMtx);
					chunkStates.emplace(newChunk);
				}
				else
				{
					// What do we do if there were no free blocks?
					g_logger_warning("No free pools for block data.");
				}
			}
			else if (retesselate)
			{
				FillChunkCommand cmd;
				cmd.chunkCoordinates = chunkCoordinates;
				cmd.type = CommandType::TesselateVertices;
				cmd.subChunks = &subChunks();
				cmd.blockData = nullptr;
				{
					std::lock_guard<std::mutex> lock(chunkMtx);
					auto iter = chunkStates.find(ChunkStateData(chunkCoordinates));
					if (iter == chunkStates.end())
					{
						return;
					}
					cmd.blockData = iter->blockData;
				}

				// Update the sub-chunks that are about to be deleted
				for (int i = 0; i < subChunks().size(); i++)
				{
					if (subChunks()[i]->chunkCoordinates == chunkCoordinates && subChunks()[i]->state == SubChunkState::Uploaded)
					{
						subChunks()[i]->state = SubChunkState::RetesselateVertices;
					}
				}

				chunkWorker().queueCommand(cmd);
			}
		}

		void queueSaveChunk(const glm::ivec2& chunkCoordinates)
		{
			// Only upload if we need to
			std::unordered_set<ChunkStateData>::iterator iter;
			{
				std::lock_guard<std::mutex> lock(chunkMtx);
				iter = chunkStates.find(ChunkStateData(chunkCoordinates));
			}
			if (iter != chunkStates.end())
			{
				if (iter->state != ChunkState::Saving && iter->blockData)
				{
					FillChunkCommand cmd;
					cmd.chunkCoordinates = chunkCoordinates;
					cmd.type = CommandType::SaveBlockData;
					cmd.blockData = iter->blockData;
					cmd.subChunks = &(subChunks());
					chunkWorker().queueCommand(cmd);
				}
			}
		}

		Block getBlock(const glm::vec3& worldPosition)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			Block* blockData = nullptr;

			std::lock_guard<std::mutex> lock(chunkMtx);
			auto iter = chunkStates.find(ChunkStateData(chunkCoords));
			if (iter != chunkStates.end())
			{
				blockData = iter->blockData;
			}
			else if (worldPosition.y >= 0 && worldPosition.y < 256)
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
			Block* blockData = nullptr;

			std::lock_guard<std::mutex> lock(chunkMtx);
			auto iter = chunkStates.find(ChunkStateData(chunkCoords));
			if (iter != chunkStates.end())
			{
				blockData = iter->blockData;
			}
			else if (worldPosition.y >= 0 && worldPosition.y < 256)
			{
				// Assume it's a chunk that's out of bounds
				g_logger_warning("Tried to set invalid block at position<%2.3f, %2.3f, %2.3f>!", worldPosition.x, worldPosition.y, worldPosition.z);
				return;
			}

			if (Chunk::setBlock(worldPosition, chunkCoords, blockData, newBlock))
			{
				retesselateChunkBlockUpdate(chunkCoords, worldPosition, blockData);
			}
		}

		void removeBlock(const glm::vec3& worldPosition)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			Block* blockData = nullptr;

			std::lock_guard<std::mutex> lock(chunkMtx);
			auto iter = chunkStates.find(chunkCoords);
			if (iter != chunkStates.end())
			{
				blockData = iter->blockData;
			}
			else if (worldPosition.y >= 0 && worldPosition.y < 256)
			{
				// Assume it's a chunk that's out of bounds
				g_logger_warning("Tried to set invalid block at position<%2.3f, %2.3f, %2.3f>!", worldPosition.x, worldPosition.y, worldPosition.z);
				return;
			}

			if (Chunk::removeBlock(worldPosition, chunkCoords, blockData))
			{
				retesselateChunkBlockUpdate(chunkCoords, worldPosition, blockData);
			}
		}

		void render(const glm::vec3& playerPosition, const glm::ivec2& playerPositionInChunkCoords, Shader& shader, const Frustum& cameraFrustum)
		{
			chunkWorker().setPlayerPosChunkCoords(playerPositionInChunkCoords);

			// TODO: Weird that I have to re-enable that here. Try to find out why?
			glEnable(GL_CULL_FACE);

			for (int i = 0; i < subChunks().size(); i++)
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
							float yCenter = subChunks()[i]->subChunkLevel * 16;
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
			glm::ivec2 playerPosChunkCoords = World::toChunkCoords(playerPosition);;
			chunkWorker().setPlayerPosChunkCoords(playerPosChunkCoords);
			static glm::ivec2 lastPlayerPosChunkCoords = playerPosChunkCoords;

			// Remove out of range chunks
			for (int i = 0; i < subChunks().size(); i++)
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
						glm::ivec2 lastLocalPos = lastPlayerPosChunkCoords - position;
						bool retesselateThisChunk =
							(lastLocalPos.x * lastLocalPos.x) + (lastLocalPos.y * lastLocalPos.y) >= ((World::ChunkRadius - 1) * (World::ChunkRadius - 1))
							&& retesselateEdges;
						ChunkManager::queueCreateChunk(position, retesselateThisChunk);
					}
				}
			}

			lastPlayerPosChunkCoords = playerPosChunkCoords;
			chunkWorker().beginWork();
		}

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
			for (int i = 0; i < subChunks().size(); i++)
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
			FRONT = 5
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
		static void loadBlock(Vertex* vertexData, const glm::ivec3& vert1, const glm::ivec3& vert2, const glm::ivec3& vert3, const glm::ivec3& vert4, const TextureFormat& texture, CUBE_FACE face, bool colorFaceBasedOnBiome, int lightLevel[6], glm::ivec3 lightColor[6]);

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
			// TODO: This is going to go backwards through the array, probably for cache efficiency
			for (int y = World::ChunkHeight - 1; y >= 0; y--)
			{
				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						// 24 Vertices per cube
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

						if (y == World::ChunkHeight - 1)
						{
							if (blockData[arrayExpansion].id == BlockMap::AIR_BLOCK.id)
							{
								blockData[arrayExpansion].lightLevel = 31;
							}
							else
							{
								// TODO: Only do this if this is not a transparent block
								blockData[arrayExpansion].lightLevel = 0;
							}
						}
						blockData[arrayExpansion].lightColor =
							((7 << 0) & 0x7)  | // R
							((7 << 3) & 0x38) | // G
							((7 << 6) & 0x1C0); // B
					}
				}
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

						if (block == BlockMap::NULL_BLOCK || block.id == BlockMap::AIR_BLOCK.id)
						{
							continue;
						}

						const BlockFormat& blockFormat = BlockMap::getBlock(blockId);
						const TextureFormat& side = BlockMap::getTextureFormat(blockFormat.sideTexture);
						const TextureFormat& top = BlockMap::getTextureFormat(blockFormat.topTexture);
						const TextureFormat& bottom = BlockMap::getTextureFormat(blockFormat.bottomTexture);

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

						// Top Face
						Block topBlockData = getBlockInternal(blockData, x, y + 1, z, chunkCoordinates);
						// Bottom Face
						Block bottomBlockData = getBlockInternal(blockData, x, y - 1, z, chunkCoordinates);
						// Right Face
						Block rightBlockData = getBlockInternal(blockData, x, y, z + 1, chunkCoordinates);
						// Left Face
						Block leftBlockData = getBlockInternal(blockData, x, y, z - 1, chunkCoordinates);
						// Forward Face
						Block forwardBlockData = getBlockInternal(blockData, x + 1, y, z, chunkCoordinates);
						// Back Face
						Block backBlockData = getBlockInternal(blockData, x - 1, y, z, chunkCoordinates);

						glm::ivec3 topBlockLightColor =
							glm::ivec3(
								((topBlockData.lightColor & 0x7) >> 0),  // R
								((topBlockData.lightColor & 0x38) >> 3), // G
								((topBlockData.lightColor & 0x1C0) >> 6) // B;
							);
						glm::ivec3 bottomBlockLightColor =
							glm::ivec3(
								((bottomBlockData.lightColor & 0x7) >> 0),  // R
								((bottomBlockData.lightColor & 0x38) >> 3), // G
								((bottomBlockData.lightColor & 0x1C0) >> 6) // B;
							);
						glm::ivec3 rightBlockLightColor =
							glm::ivec3(
								((rightBlockData.lightColor & 0x7) >> 0),  // R
								((rightBlockData.lightColor & 0x38) >> 3), // G
								((rightBlockData.lightColor & 0x1C0) >> 6) // B;
							);
						glm::ivec3 leftBlockLightColor =
							glm::ivec3(
								((leftBlockData.lightColor & 0x7) >> 0),  // R
								((leftBlockData.lightColor & 0x38) >> 3), // G
								((leftBlockData.lightColor & 0x1C0) >> 6) // B;
							);
						glm::ivec3 forwardBlockLightColor =
							glm::ivec3(
								((forwardBlockData.lightColor & 0x7) >> 0),  // R
								((forwardBlockData.lightColor & 0x38) >> 3), // G
								((forwardBlockData.lightColor & 0x1C0) >> 6) // B;
							);
						glm::ivec3 backBlockLightColor =
							glm::ivec3(
								((backBlockData.lightColor & 0x7) >> 0),  // R
								((backBlockData.lightColor & 0x38) >> 3), // G
								((backBlockData.lightColor & 0x1C0) >> 6) // B;
							);

						int lightLevels[6];
						lightLevels[(int)CUBE_FACE::TOP] = topBlockData.lightLevel;
						lightLevels[(int)CUBE_FACE::BOTTOM] = bottomBlockData.lightLevel;
						lightLevels[(int)CUBE_FACE::RIGHT] = rightBlockData.lightLevel;
						lightLevels[(int)CUBE_FACE::LEFT] = leftBlockData.lightLevel;
						lightLevels[(int)CUBE_FACE::FRONT] = forwardBlockData.lightLevel;
						lightLevels[(int)CUBE_FACE::BACK] = backBlockData.lightLevel;

						glm::ivec3 lightColors[6];
						lightColors[(int)CUBE_FACE::TOP] = topBlockLightColor;
						lightColors[(int)CUBE_FACE::BOTTOM] = bottomBlockLightColor;
						lightColors[(int)CUBE_FACE::RIGHT] = rightBlockLightColor;
						lightColors[(int)CUBE_FACE::LEFT] = leftBlockLightColor;
						lightColors[(int)CUBE_FACE::FRONT] = forwardBlockLightColor;
						lightColors[(int)CUBE_FACE::BACK] = backBlockLightColor;

						const BlockFormat& topBlock = BlockMap::getBlock(topBlockData.id);
						if (topBlockData.id && topBlock.isTransparent)
						{
							currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
							if (!currentSubChunk)
							{
								// TODO: Handle running out of memory better than this
								break;
							}
							loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[5], verts[6], verts[7], verts[4],
								top, CUBE_FACE::TOP, blockFormat.colorTopByBiome, lightLevels, lightColors);
							currentSubChunk->numVertsUsed += 6;
						}

						// Bottom Face
						const BlockFormat& bottomBlock = BlockMap::getBlock(bottomBlockData.id);
						if (bottomBlockData.id && bottomBlock.isTransparent)
						{
							currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
							if (!currentSubChunk)
							{
								// TODO: Handle running out of memory better than this
								break;
							}
							loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[0], verts[3], verts[2], verts[1],
								bottom, CUBE_FACE::BOTTOM, blockFormat.colorBottomByBiome, lightLevels, lightColors);
							currentSubChunk->numVertsUsed += 6;
						}

						const BlockFormat& rightBlock = BlockMap::getBlock(rightBlockData.id);
						if (rightBlockData.id && rightBlock.isTransparent)
						{
							currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
							if (!currentSubChunk)
							{
								// TODO: Handle running out of memory better than this
								break;
							}
							loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[2], verts[6], verts[5], verts[1],
								side, CUBE_FACE::RIGHT, blockFormat.colorSideByBiome, lightLevels, lightColors);
							currentSubChunk->numVertsUsed += 6;
						}

						const BlockFormat& leftBlock = BlockMap::getBlock(leftBlockData.id);
						if (leftBlockData.id && leftBlock.isTransparent)
						{
							currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
							if (!currentSubChunk)
							{
								// TODO: Handle running out of memory better than this
								break;
							}
							loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[0], verts[4], verts[7], verts[3],
								side, CUBE_FACE::LEFT, blockFormat.colorSideByBiome, lightLevels, lightColors);
							currentSubChunk->numVertsUsed += 6;
						}

						const BlockFormat& forwardBlock = BlockMap::getBlock(forwardBlockData.id);
						if (forwardBlockData.id && forwardBlock.isTransparent)
						{
							currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
							if (!currentSubChunk)
							{
								// TODO: Handle running out of memory better than this
								break;
							}
							loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[7], verts[6], verts[2], verts[3],
								side, CUBE_FACE::FRONT, blockFormat.colorSideByBiome, lightLevels, lightColors);
							currentSubChunk->numVertsUsed += 6;
						}

						const BlockFormat& backBlock = BlockMap::getBlock(backBlockData.id);
						if (backBlockData.id && backBlock.isTransparent)
						{
							currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
							if (!currentSubChunk)
							{
								// TODO: Handle running out of memory better than this
								break;
							}
							loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[0], verts[1], verts[5], verts[4],
								side, CUBE_FACE::BACK, blockFormat.colorSideByBiome, lightLevels, lightColors);
							currentSubChunk->numVertsUsed += 6;
						}
					}
				}
			}

			if (currentSubChunk && currentSubChunk->numVertsUsed > 0)
			{
				currentSubChunk->state = SubChunkState::UploadVerticesToGpu;
			}

			for (int i = 0; i < (*subChunks).size(); i++)
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
			return x >= 16 || x < 0 || z >= 16 || z < 0
				? ChunkManager::getBlock(glm::vec3(chunkCoordinates.x * 16.0f + x, y, chunkCoordinates.y * 16.0f + z))
				: y >= 256 || y < 0
				? BlockMap::NULL_BLOCK
				: data[index];
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
			data[index].lightLevel = 15;
			data[index].lightColor =
				((6 << 0) & 0x7) | // R
				((6 << 3) & 0x38) | // G
				((1 << 6) & 0x1C0); // B
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

			data[index] = BlockMap::AIR_BLOCK;
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
			data2 |= (((uint32)(lightLevel << 4) & LIGHT_LEVEL_BITMASK));
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
			int lightLevels[6],
			glm::ivec3 lightColors[6])
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

			glm::ivec3 lightColor0 = lightColors[(int)CUBE_FACE::TOP] + lightColors[(int)CUBE_FACE::BACK] + lightColors[(int)CUBE_FACE::LEFT] / 3;
			glm::ivec3 lightColor1 = lightColors[(int)CUBE_FACE::TOP] + lightColors[(int)CUBE_FACE::BACK] + lightColors[(int)CUBE_FACE::RIGHT] / 3;;
			glm::ivec3 lightColor2 = lightColors[(int)CUBE_FACE::TOP] + lightColors[(int)CUBE_FACE::FRONT] + lightColors[(int)CUBE_FACE::LEFT] / 3;;
			glm::ivec3 lightColor3 = lightColors[(int)CUBE_FACE::TOP] + lightColors[(int)CUBE_FACE::FRONT] + lightColors[(int)CUBE_FACE::RIGHT] / 3;;

			int lightLevel0 = lightLevels[(int)CUBE_FACE::TOP] + lightLevels[(int)CUBE_FACE::BACK] + lightLevels[(int)CUBE_FACE::LEFT] / 3;
			int lightLevel1 = lightLevels[(int)CUBE_FACE::TOP] + lightLevels[(int)CUBE_FACE::BACK] + lightLevels[(int)CUBE_FACE::RIGHT] / 3;;
			int lightLevel2 = lightLevels[(int)CUBE_FACE::TOP] + lightLevels[(int)CUBE_FACE::FRONT] + lightLevels[(int)CUBE_FACE::LEFT] / 3;;
			int lightLevel3 = lightLevels[(int)CUBE_FACE::TOP] + lightLevels[(int)CUBE_FACE::FRONT] + lightLevels[(int)CUBE_FACE::RIGHT] / 3;;

			vertexData[0] = compress(vert1, texture, face, uv0, colorFaceBasedOnBiome, lightLevel0, lightColor0);
			vertexData[1] = compress(vert2, texture, face, uv1, colorFaceBasedOnBiome, lightLevel1, lightColor1);
			vertexData[2] = compress(vert3, texture, face, uv2, colorFaceBasedOnBiome, lightLevel2, lightColor2);

			vertexData[3] = compress(vert1, texture, face, uv3, colorFaceBasedOnBiome, lightLevel0, lightColor0);
			vertexData[4] = compress(vert3, texture, face, uv4, colorFaceBasedOnBiome, lightLevel2, lightColor2);
			vertexData[5] = compress(vert4, texture, face, uv5, colorFaceBasedOnBiome, lightLevel3, lightColor3);
		}
	}
}