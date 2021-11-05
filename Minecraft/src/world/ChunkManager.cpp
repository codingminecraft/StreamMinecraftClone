#include "world/ChunkManager.h"
#include "world/World.h"
#include "world/BlockMap.h"
#include "world/Chunk.hpp"
#include "core/Pool.hpp"
#include "core/File.h"
#include "utils/DebugStats.h"
#include "utils/CMath.h"
#include "utils/Constants.h"
#include "renderer/Shader.h"
#include "renderer/Renderer.h"
#include "renderer/Frustum.h"

namespace Minecraft
{
	enum class CommandType : uint8
	{
		SaveBlockData = 0,
		GenerateTerrain = 1,
		GenerateDecorations = 2,
		CalculateLighting = 3,
		RecalculateLighting = 4,
		TesselateVertices = 5
	};

	struct FillChunkCommand
	{
		// Must be at least ChunkWidth * ChunkDepth * ChunkHeight blocks available
		Chunk* chunk;
		Pool<SubChunk, World::ChunkCapacity * 16>* subChunks;
		glm::ivec2 playerPosChunkCoords;
		CommandType type;
		glm::vec3 blockThatUpdated;
	};

	namespace ChunkPrivate
	{
		void generateTerrain(Chunk* chunk, const glm::ivec2& chunkCoordinates, float seed);
		void generateDecorations(const glm::ivec2& lastPlayerLoadPosChunkCoords, float seed);
		// Must guarantee at least 16 sub-chunks located at this address
		void generateRenderData(Pool<SubChunk, World::ChunkCapacity * 16>* subChunks, const Chunk* chunk, const glm::ivec2& chunkCoordinates);
		void calculateLighting(Chunk* chunk, const glm::ivec2& chunkCoordinates);
		void calculateLightingUpdate(Chunk* chunk, const glm::ivec2& chunkCoordinates, const glm::vec3& blockPosition);

		Block getLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, const Chunk* blockData);
		Block getBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, const Chunk* blockData);
		bool setLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, Chunk* blockData, Block newBlock);
		bool setBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, Chunk* blockData, Block newBlock);
		bool removeLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, Chunk* blockData);
		bool removeBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, Chunk* blockData);

		void serialize(const std::string& worldSavePath, const Block* blockData, const glm::ivec2& chunkCoordinates);
		void deserialize(Block* blockData, const std::string& worldSavePath, const glm::ivec2& chunkCoordinates);

		bool exists(const std::string& worldSavePath, const glm::ivec2& chunkCoordinates);
		void info();
	}

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
			glm::ivec2 tmpA = a.playerPosChunkCoords - a.chunk->chunkCoords;
			int32 aDistanceSquared = (tmpA.x * tmpA.x) + (tmpA.y * tmpA.y);
			glm::ivec2 tmpB = b.playerPosChunkCoords - b.chunk->chunkCoords;
			int32 bDistanceSquared = (tmpB.x * tmpB.x) + (tmpB.y * tmpB.y);
			return aDistanceSquared > bDistanceSquared;
		}
	};

	namespace ChunkManager
	{
		extern bool stepOnce = false;
		extern bool doStepLogic = false;
		extern std::atomic<glm::vec3> backPropagationLocation = glm::vec3();

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
							cv.wait(lock, [&] { return (!doWork || !commands.empty()) && !waitingOnCommand; });
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
						case CommandType::GenerateTerrain:
						{
							if (ChunkPrivate::exists(World::chunkSavePath, command.chunk->chunkCoords))
							{
								ChunkPrivate::deserialize(command.chunk->data, World::chunkSavePath, command.chunk->chunkCoords);
								//ChunkManager::setNeedsGenerateDecorations(command.chunkCoordinates, false);
							}
							else
							{
								ChunkPrivate::generateTerrain(command.chunk, command.chunk->chunkCoords, World::seedAsFloat);
								//ChunkManager::setNeedsGenerateDecorations(command.chunkCoordinates, true);
							}
						}
						break;
						case CommandType::GenerateDecorations:
						{
							//waitingOnCommand = true;
							//ChunkPrivate::generateDecorations(command.playerPosChunkCoords, World::seedAsFloat);
							//waitingOnCommand = false;
						}
						break;
						case CommandType::CalculateLighting:
						{
							ChunkPrivate::calculateLighting(command.chunk, command.chunk->chunkCoords);
						}
						break;
						case CommandType::RecalculateLighting:
						{
							ChunkPrivate::calculateLightingUpdate(command.chunk, command.chunk->chunkCoords, command.blockThatUpdated);
						}
						break;
						case CommandType::TesselateVertices:
						{
							ChunkPrivate::generateRenderData(command.subChunks, command.chunk, command.chunk->chunkCoords);
						}
						break;
						case CommandType::SaveBlockData:
						{
							// Unload all sub-chunks
							for (int i = 0; i < (int)command.subChunks->size(); i++)
							{
								if ((*command.subChunks)[i]->state == SubChunkState::Uploaded && (*command.subChunks)[i]->chunkCoordinates == command.chunk->chunkCoords)
								{
									(*command.subChunks)[i]->state = SubChunkState::Unloaded;
									(*command.subChunks)[i]->numVertsUsed = 0;
									command.subChunks->freePool(i);
									DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed - (World::MaxVertsPerSubChunk * sizeof(Vertex));
								}
							}

							// Serialize block data
							ChunkPrivate::serialize(World::chunkSavePath, command.chunk->data, command.chunk->chunkCoords);

							// Tell the chunk manager we are done
							ChunkManager::unloadChunk(command.chunk->chunkCoords);
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
			std::atomic<bool> waitingOnCommand = false;
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
		static void retesselateChunkBlockUpdate(const glm::ivec2& chunkCoords, const glm::vec3& worldPosition, Chunk* blockData);

		// Internal variables
		static std::mutex chunkMtx;
		static uint32 processorCount = 0;
		static robin_hood::unordered_node_map<glm::ivec2, Chunk> chunks = {};
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
			processorCount = 1;// std::thread::hardware_concurrency();

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
			chunks.clear();

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

			for (robin_hood::pair<const glm::ivec2, Chunk>& chunkIter : chunks)
			{
				Chunk& chunk = chunkIter.second;
				Block* blockData = chunk.data;
				if (chunk.state != ChunkState::Saving && blockData)
				{
					cmd.chunk = &chunk;
					chunkWorker().queueCommand(cmd);
				}
			}
		}

		void queueCreateChunk(const glm::ivec2& chunkCoordinates)
		{
			// Only upload if we need to
			Chunk* chunk = getChunk(chunkCoordinates);
			if (!chunk)
			{
				if (chunkFreeList.size() > 0)
				{
					Chunk newChunk;
					newChunk.data = chunkFreeList.front();
					chunkFreeList.pop_front();

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
					cmd.subChunks = &(subChunks());

					// Queue the fill command
					chunkWorker().queueCommand(cmd);
					// Queue the calculate lighting command
					cmd.type = CommandType::CalculateLighting;
					chunkWorker().queueCommand(cmd);
					// Queue the tesselate command
					cmd.type = CommandType::TesselateVertices;
					chunkWorker().queueCommand(cmd);

					DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed + blockPool().poolSize() * sizeof(Block);
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
			Chunk* chunk = getChunk(chunkCoordinates);
			if (chunk)
			{
				FillChunkCommand cmd;
				cmd.type = CommandType::RecalculateLighting;
				cmd.subChunks = &subChunks();
				cmd.chunk = chunk;
				cmd.blockThatUpdated = blockPositionThatUpdated;

				chunkWorker().queueCommand(cmd);
				queueRetesselateChunk(chunkCoordinates, chunk);
			}
		}

		void queueRetesselateChunk(const glm::ivec2& chunkCoordinates, Chunk* chunk, bool doImmediately)
		{
			if (!chunk)
			{
				chunk = getChunk(chunkCoordinates);
			}

			if (chunk)
			{
				FillChunkCommand cmd;
				cmd.type = CommandType::TesselateVertices;
				cmd.subChunks = &subChunks();
				cmd.chunk = chunk;

				// Update the sub-chunks that are about to be deleted
				for (int i = 0; i < (int)subChunks().size(); i++)
				{
					if (subChunks()[i]->chunkCoordinates == chunkCoordinates && subChunks()[i]->state == SubChunkState::Uploaded)
					{
						subChunks()[i]->state = SubChunkState::RetesselateVertices;
					}
				}

				// TODO: Remove this flag, this is mostly for debugging
				if (!doImmediately)
				{
					chunkWorker().queueCommand(cmd);
					chunkWorker().beginWork();
				}
				else
				{
					ChunkPrivate::generateRenderData(&subChunks(), chunk, chunk->chunkCoords);
				}
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
					FillChunkCommand cmd;
					cmd.type = CommandType::SaveBlockData;
					cmd.chunk = chunk;
					cmd.subChunks = &(subChunks());
					chunkWorker().queueCommand(cmd);
				}
			}
		}

		void queueGenerateDecorations(const glm::ivec2& lastPlayerLoadChunkPos)
		{
			FillChunkCommand cmd;
			cmd.type = CommandType::GenerateDecorations;
			cmd.playerPosChunkCoords = lastPlayerLoadChunkPos;
			chunkWorker().queueCommand(cmd);
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
				queueRecalculateLighting(chunkCoords, worldPosition);
				chunkWorker().beginWork();
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

			if (ChunkPrivate::removeBlock(worldPosition, chunkCoords, chunk))
			{
				retesselateChunkBlockUpdate(chunkCoords, worldPosition, chunk);
				queueRecalculateLighting(chunkCoords, worldPosition);
				chunkWorker().beginWork();
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
				//std::lock_guard<std::mutex> lock(chunkMtx);
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
				chunk.topNeighbor = getChunk(chunk.chunkCoords + INormals2::Up);
				chunk.bottomNeighbor = getChunk(chunk.chunkCoords + INormals2::Down);
				chunk.leftNeighbor = getChunk(chunk.chunkCoords + INormals2::Left);
				chunk.rightNeighbor = getChunk(chunk.chunkCoords + INormals2::Right);
			}
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
					const auto& iter = chunks.find(chunkPos);
					if (iter == chunks.end() && subChunks()[i]->state != SubChunkState::TesselatingVertices)
					{
						// If the chunk coords are no longer loaded, set this chunk as not in use anymore
						subChunks()[i]->state = SubChunkState::Unloaded;
						subChunks()[i]->numVertsUsed = 0;
						subChunks().freePool(i);
					}
					else if (iter != chunks.end() && iter->second.state == ChunkState::Loaded)
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
			// TODO: Ensure this is only ever called from the main thread
			//std::lock_guard lock(chunkMtx);
			auto& iter = chunks.find(chunkCoordinates);
			// TODO: Investigate why sometimes iter->blockData is nullptr
			// I think it might be because I wasn't locking the mutex, but double check just in case
			if (iter != chunks.end() && iter->second.state != ChunkState::Saving)
			{
				DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed - (float)(blockPool().poolSize() * sizeof(Block));

				chunkFreeList.push_back(iter->second.data);
				chunks.erase(iter);
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
						auto& iter = chunks.find(chunkPos);
						if (iter != chunks.end() && iter->second.state != ChunkState::Saving)
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

			ChunkManager::queueGenerateDecorations(playerPosChunkCoords);
			lastPlayerPosChunkCoords = playerPosChunkCoords;

			ChunkManager::patchChunkPointers();
			if (needsWork)
			{
				chunkWorker().beginWork();
			}
		}

		// TODO: Simplify me!
		static void retesselateChunkBlockUpdate(const glm::ivec2& chunkCoords, const glm::vec3& worldPosition, Chunk* chunk)
		{
			FillChunkCommand cmd;
			cmd.type = CommandType::TesselateVertices;
			cmd.subChunks = &subChunks();
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
			for (int i = 0; i < (int)subChunks().size(); i++)
			{
				if (subChunks()[i]->state == SubChunkState::Uploaded)
				{
					for (int j = 0; j < numChunksToUpdate; j++)
					{
						if (subChunks()[i]->chunkCoordinates == chunksToUpdate[j]->chunkCoords)
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
				cmd.chunk = chunksToUpdate[i];
				chunkWorker().queueCommand(cmd);
			}
			chunkWorker().beginWork();
		}
	}

	namespace ChunkPrivate
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
		static Block getBlockInternal(const Chunk* chunk, int x, int y, int z);
		static bool setBlockInternal(Chunk* chunk, int x, int y, int z, Block newBlock);
		static bool removeBlockInternal(Chunk* chunk, int x, int y, int z);
		static std::string getFormattedFilepath(const glm::ivec2& chunkCoordinates, const std::string& worldSavePath);
		static void loadBlock(Vertex* vertexData, const glm::ivec3& vert1, const glm::ivec3& vert2, const glm::ivec3& vert3, const glm::ivec3& vert4, const TextureFormat& texture, CUBE_FACE face, bool colorFaceBasedOnBiome, int lightLevel, const glm::ivec3& lightColor);
		static void updateBlockLightLevel(Chunk* chunk, int localX, int localY, int localZ, const glm::ivec2& chunkCoordinates, bool zeroOut, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate = robin_hood::unordered_flat_set<Chunk*>());

		void info()
		{
			g_logger_info("%d size of chunk", sizeof(Block) * World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);
			g_logger_info("Max %d size of vertex data", sizeof(Vertex) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth * 24);
		}

		void generateTerrain(Chunk* chunk, const glm::ivec2& chunkCoordinates, float seed)
		{
			const int worldChunkX = chunkCoordinates.x * 16;
			const int worldChunkZ = chunkCoordinates.y * 16;

			g_memory_zeroMem(chunk->data, sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth);
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
							chunk->data[arrayExpansion].id = 7;
						}
						else if (y < stoneHeight)
						{
							// Stone
							chunk->data[arrayExpansion].id = 6;
						}
						else if (y < maxHeight)
						{
							// Dirt
							chunk->data[arrayExpansion].id = 4;
						}
						else if (y == maxHeight)
						{
							// Grass
							chunk->data[arrayExpansion].id = 2;
						}
						else if (!chunk->data[arrayExpansion].id)
						{
							chunk->data[arrayExpansion].id = BlockMap::AIR_BLOCK.id;
						}
					}
				}
			}
		}

		void generateDecorations(const glm::ivec2& lastPlayerLoadPosChunkCoords, float seed)
		{
			//auto& allStates = ChunkManager::getAllChunkStates();

			//for (int chunkZ = lastPlayerLoadPosChunkCoords.y - World::ChunkRadius; chunkZ <= lastPlayerLoadPosChunkCoords.y + World::ChunkRadius; chunkZ++)
			//{
			//	for (int chunkX = lastPlayerLoadPosChunkCoords.x - World::ChunkRadius; chunkX <= lastPlayerLoadPosChunkCoords.x + World::ChunkRadius; chunkX++)
			//	{
			//		const int worldChunkX = chunkX * 16;
			//		const int worldChunkZ = chunkZ * 16;

			//		const SimplexNoise generator = SimplexNoise(World::seedAsFloat.load());
			//		const float scale = 0.001f;
			//		glm::ivec2 chunkCoords = glm::ivec2(chunkX, chunkZ);
			//		auto iter = allStates.find(ChunkStateData(chunkCoords));
			//		if (iter == allStates.end())
			//		{
			//			continue;
			//		}

			//		ChunkStateData state = *iter;
			//		Block* blockData = state.blockData;
			//		if (CMath::length2(glm::ivec2(chunkX, chunkZ) - lastPlayerLoadPosChunkCoords) > (World::ChunkRadius - 1) * (World::ChunkRadius - 1))
			//		{
			//			// Skip over all chunks in range radius - 1
			//			continue;
			//		}

			//		if (!state.needsToGenerateDecorations)
			//		{
			//			continue;
			//		}
			//		state.needsToGenerateDecorations = false;
			//		allStates.erase(iter);
			//		allStates.insert(state);

			//		for (int y = 0; y < World::ChunkHeight; y++)
			//		{
			//			for (int x = 0; x < World::ChunkDepth; x++)
			//			{
			//				for (int z = 0; z < World::ChunkWidth; z++)
			//				{
			//					if (blockData[to1DArray(x, y, z)].id == 2)
			//					{
			//						// Generate some trees 
			//						int num = (rand() % 100);
			//						bool generateTree = num > 98;
			//						int treeHeight = (rand() % 6) + 3;
			//						int leavesBottomY = glm::clamp(treeHeight - 3, 3, (int)World::ChunkHeight - 1);
			//						int leavesTopY = treeHeight + 1;
			//						if (generateTree && (y + 1 + leavesTopY < World::ChunkHeight))
			//						{
			//							for (int treeY = y + 1; treeY <= treeHeight + y; treeY++)
			//							{
			//								blockData[to1DArray(x, treeY, z)].id = 8;
			//							}

			//							int ringLevel = 0;
			//							for (int leavesY = leavesBottomY + y; leavesY <= leavesTopY + y; leavesY++)
			//							{
			//								int leafRadius = leavesY == leavesTopY ? 2 : 1;
			//								for (int leavesX = x - leafRadius; leavesX <= x + leafRadius; leavesX++)
			//								{
			//									for (int leavesZ = z - leafRadius; leavesZ <= z + leafRadius; leavesZ++)
			//									{
			//										if (leavesX < World::ChunkDepth && leavesX >= 0 && leavesZ < World::ChunkWidth && leavesZ >= 0)
			//										{
			//											blockData[to1DArray(leavesX, leavesY, leavesZ)].id = 9;
			//										}
			//										else
			//										{
			//											glm::vec3 blockPosition = glm::vec3(worldChunkX + leavesX, leavesY, worldChunkZ + leavesZ);
			//											glm::ivec2 blockToCheckChunkPos = World::toChunkCoords(blockPosition);
			//											glm::ivec3 localPosition = glm::floor(blockPosition - glm::vec3(blockToCheckChunkPos.x * 16.0f, 0.0f, blockToCheckChunkPos.y * 16.0f));

			//											Block* otherChunk = nullptr;
			//											auto iter = allStates.find(ChunkStateData(glm::ivec2(chunkX, chunkZ)));
			//											if (iter != allStates.end())
			//											{
			//												otherChunk = iter->blockData;
			//												otherChunk[to1DArray(localPosition.x, localPosition.y, localPosition.z)].id = 9;
			//											}
			//										}
			//									}
			//								}
			//								ringLevel++;
			//							}
			//						}
			//					}
			//				}
			//			}
			//		}
			//	}
			//}
		}

		void calculateLighting(Chunk* chunk, const glm::ivec2& chunkCoordinates)
		{
			// First set all the sky blocks and reset any non-sky blocks to 0 unless they are a source
			for (int y = World::ChunkHeight - 1; y >= 0; y--)
			{
				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						int arrayExpansion = to1DArray(x, y, z);
						if (chunk->data[arrayExpansion] != BlockMap::AIR_BLOCK)
						{
							continue;
						}

						// If the 32 bit is set in the block above, this is a sky block
						const Block& topNeighbor = y + 1 >= 256 ? BlockMap::NULL_BLOCK : chunk->data[to1DArray(x, y + 1, z)];
						int topNeighborLight = topNeighbor == BlockMap::AIR_BLOCK ? topNeighbor.lightLevel & 31 : 0;

						bool isSkyblock = (topNeighbor.lightLevel & 32) || (y == World::ChunkHeight - 1);
						if (isSkyblock)
						{
							chunk->data[arrayExpansion].lightLevel = 31 | 32;
						}
						else
						{
							chunk->data[arrayExpansion].lightLevel = 0;
						}

						chunk->data[arrayExpansion].lightColor =
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
						if (chunk->data[arrayExpansion] != BlockMap::AIR_BLOCK)
						{
							continue;
						}

						bool isSkyblock = chunk->data[arrayExpansion].lightLevel & 32;
						if (!isSkyblock)
						{
							// If the 32 bit is set in the block above, this is a sky block
							const Block& topNeighbor = chunk->data[to1DArray(x, y + 1, z)];
							int topNeighborLight = topNeighbor.calculatedLightLevel();
							Block bottomNeighbor = getBlockInternal(chunk, x, y - 1, z);
							int bottomNeighborLight = bottomNeighbor.calculatedLightLevel();
							Block leftNeighbor = getBlockInternal(chunk, x, y, z - 1);
							int leftNeighborLight = leftNeighbor.calculatedLightLevel();
							Block rightNeighbor = getBlockInternal(chunk, x, y, z + 1);
							int rightNeighborLight = rightNeighbor.calculatedLightLevel();
							Block frontNeighbor = getBlockInternal(chunk, x + 1, y, z);
							int frontNeighborLight = frontNeighbor.calculatedLightLevel();
							Block backNeighbor = getBlockInternal(chunk, x - 1, y, z);
							int backNeighborLight = backNeighbor.calculatedLightLevel();

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
							if (newLightLevel != (chunk->data[arrayExpansion].lightLevel & 31))
							{
								updateBlockLightLevel(chunk, x, y, z, chunkCoordinates, false);
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

		void calculateLightingUpdate(Chunk* chunk, const glm::ivec2& chunkCoordinates, const glm::vec3& blockPosition)
		{
			glm::ivec3 localPosition = glm::floor(blockPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			int localX = localPosition.x;
			int localY = localPosition.y;
			int localZ = localPosition.z;
			Block blockThatsUpdating = chunk->data[to1DArray(localX, localY, localZ)];
			if (!blockThatsUpdating.isLightSource())
			{
				// Update all blocks surrounding the block that was just placed
				for (int i = 0; i < INormals3::CardinalDirections.size(); i++)
				{
					glm::ivec3 blockToCheck = localPosition + INormals3::CardinalDirections[i];
					// If the block is outside this chunk, update the correct chunk and block
					if (blockToCheck.x >= World::ChunkDepth)
					{
						if (chunk->topNeighbor)
						{
							updateBlockLightLevel(chunk->topNeighbor, blockToCheck.x - World::ChunkDepth, blockToCheck.y, blockToCheck.z, chunkCoordinates, true);
						}
					}
					else if (blockToCheck.x < 0)
					{
						if (chunk->bottomNeighbor)
						{
							updateBlockLightLevel(chunk->bottomNeighbor, World::ChunkDepth + blockToCheck.x, blockToCheck.y, blockToCheck.z, chunkCoordinates, true);
						}
					}
					else if (blockToCheck.z >= World::ChunkWidth)
					{
						if (chunk->rightNeighbor)
						{
							updateBlockLightLevel(chunk->rightNeighbor, blockToCheck.x, blockToCheck.y, blockToCheck.z - World::ChunkWidth, chunkCoordinates, true);
						}
					}
					else if (blockToCheck.z < 0)
					{
						if (chunk->leftNeighbor)
						{
							updateBlockLightLevel(chunk->leftNeighbor, blockToCheck.x, blockToCheck.y, World::ChunkDepth + blockToCheck.z, chunkCoordinates, true);
						}
					}
					else
					{
						updateBlockLightLevel(chunk, blockToCheck.x, blockToCheck.y, blockToCheck.z, chunkCoordinates, true);
					}
				}
			}
			else
			{
				updateBlockLightLevel(chunk, localX, localY, localZ, chunkCoordinates, false);
			}
		}

		Block getLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, const Chunk* chunk)
		{
			return getBlockInternal(chunk, localPosition.x, localPosition.y, localPosition.z);
		}

		Block getBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, const Chunk* chunk)
		{
			glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			return getLocalBlock(localPosition, chunkCoordinates, chunk);
		}

		bool setLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, Chunk* chunk, Block newBlock)
		{
			return setBlockInternal(chunk, localPosition.x, localPosition.y, localPosition.z, newBlock);
		}

		bool setBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, Chunk* chunk, Block newBlock)
		{
			glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			return setLocalBlock(localPosition, chunkCoordinates, chunk, newBlock);
		}

		bool removeLocalBlock(const glm::ivec3& localPosition, const glm::ivec2& chunkCoordinates, Chunk* chunk)
		{
			return removeBlockInternal(chunk, localPosition.x, localPosition.y, localPosition.z);
		}

		bool removeBlock(const glm::vec3& worldPosition, const glm::ivec2& chunkCoordinates, Chunk* chunk)
		{
			glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			return removeLocalBlock(localPosition, chunkCoordinates, chunk);
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

		void generateRenderData(Pool<SubChunk, World::ChunkCapacity * 16>* subChunks, const Chunk* chunk, const glm::ivec2& chunkCoordinates)
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
						const Block& block = getBlockInternal(chunk, x, y, z);
						int blockId = block.id;

						if (block == BlockMap::NULL_BLOCK || block == BlockMap::AIR_BLOCK)
						{
							continue;
						}

						const BlockFormat& blockFormat = BlockMap::getBlock(blockId);

						// TODO: SIMDify this section
						static glm::ivec3 verts[8];
						verts[0] = glm::ivec3(
							x,
							y,
							z
						);
						verts[1] = verts[0] + INormals3::Right;
						verts[2] = verts[1] + INormals3::Front;
						verts[3] = verts[0] + INormals3::Front;

						verts[4] = verts[0] + INormals3::Up;
						verts[5] = verts[1] + INormals3::Up;
						verts[6] = verts[2] + INormals3::Up;
						verts[7] = verts[3] + INormals3::Up;

						// The order of coordinates is LEFT, RIGHT, BOTTOM, TOP, BACK, FRONT blocks to check
						int xCoords[6] = { x, x, x, x, x - 1, x + 1 };
						int yCoords[6] = { y, y, y - 1, y + 1, y, y };
						int zCoords[6] = { z - 1, z + 1, z, z, z, z };

						Block blocks[6];
						glm::ivec3 lightColors[6];
						int lightLevels[6];
						const TextureFormat* textures[6] = {
							blockFormat.sideTexture,
							blockFormat.sideTexture,
							blockFormat.bottomTexture,
							blockFormat.topTexture,
							blockFormat.sideTexture,
							blockFormat.sideTexture
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
							blocks[i] = getBlockInternal(chunk, xCoords[i], yCoords[i], zCoords[i]);
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

		static Block getBlockInternal(const Chunk* chunk, int x, int y, int z)
		{
			if (!chunk)
			{
				return BlockMap::NULL_BLOCK;
			}

			if (x >= World::ChunkDepth || x < 0 || z >= World::ChunkWidth || z < 0)
			{
				if (x >= World::ChunkDepth)
				{
					return getBlockInternal(chunk->topNeighbor, x - World::ChunkDepth, y, z);
				}
				else if (x < 0)
				{
					return getBlockInternal(chunk->bottomNeighbor, World::ChunkDepth + x, y, z);
				}

				if (z >= World::ChunkWidth)
				{
					return getBlockInternal(chunk->rightNeighbor, x, y, z - World::ChunkWidth);
				}
				else if (z < 0)
				{
					return getBlockInternal(chunk->leftNeighbor, x, y, World::ChunkWidth + z);
				}
			}
			else if (y >= World::ChunkHeight || y < 0)
			{
				return BlockMap::NULL_BLOCK;
			}

			int index = to1DArray(x, y, z);
			return chunk->data[index];
		}

		static bool setBlockInternal(Chunk* chunk, int x, int y, int z, Block newBlock)
		{
			if (!chunk)
			{
				return false;
			}

			if (x >= World::ChunkDepth || x < 0 || z >= World::ChunkWidth || z < 0)
			{
				if (x >= World::ChunkDepth)
				{
					return setBlockInternal(chunk->topNeighbor, x - World::ChunkDepth, y, z, newBlock);
				}
				else if (x < 0)
				{
					return setBlockInternal(chunk->bottomNeighbor, World::ChunkDepth + x, y, z, newBlock);
				}

				if (z >= World::ChunkWidth)
				{
					return setBlockInternal(chunk->rightNeighbor, x, y, z - World::ChunkWidth, newBlock);
				}
				else if (z < 0)
				{
					return setBlockInternal(chunk->leftNeighbor, x, y, World::ChunkWidth + z, newBlock);
				}
			}
			else if (y >= World::ChunkHeight || y < 0)
			{
				return false;
			}

			int index = to1DArray(x, y, z);
			chunk->data[index] = newBlock;
			chunk->data[index].lightLevel = 0;// BlockMap::getBlock(newBlock.id).lightLevel;

			return true;
		}

		static bool removeBlockInternal(Chunk* chunk, int x, int y, int z)
		{
			if (!chunk)
			{
				return false;
			}

			if (x >= World::ChunkDepth || x < 0 || z >= World::ChunkWidth || z < 0)
			{
				if (x >= World::ChunkDepth)
				{
					return removeBlockInternal(chunk->topNeighbor, x - World::ChunkDepth, y, z);
				}
				else if (x < 0)
				{
					return removeBlockInternal(chunk->bottomNeighbor, World::ChunkDepth + x, y, z);
				}

				if (z >= World::ChunkWidth)
				{
					return removeBlockInternal(chunk->rightNeighbor, x, y, z - World::ChunkWidth);
				}
				else if (z < 0)
				{
					return removeBlockInternal(chunk->leftNeighbor, x, y, World::ChunkWidth + z);
				}
			}
			else if (y >= World::ChunkHeight || y < 0)
			{
				return false;
			}

			int index = to1DArray(x, y, z);
			chunk->data[index] = BlockMap::AIR_BLOCK;
			chunk->data[index].lightColor =
				((7 << 0) & 0x7) | // R
				((7 << 3) & 0x38) | // G
				((7 << 6) & 0x1C0); // B
			chunk->data[index].lightLevel = 0;

			return true;
		}

		static bool checkPositionInBounds(Chunk** currentChunk, int* x, int* z)
		{
			// Do a while loop, because the block could theoretically be like -32 which would be two chunks out
			while (*x < 0)
			{
				*currentChunk = (*currentChunk)->bottomNeighbor;
				*x = World::ChunkDepth + *x;
				if (!(*currentChunk))
				{
					return false;
				}
			}
			while (*z < 0)
			{
				*currentChunk = (*currentChunk)->leftNeighbor;
				*z = World::ChunkWidth + *z;
				if (!(*currentChunk))
				{
					return false;
				}
			}
			while (*x >= World::ChunkDepth)
			{
				*currentChunk = (*currentChunk)->topNeighbor;
				*x = *x - World::ChunkDepth;
				if (!(*currentChunk))
				{
					return false;
				}
			}
			while (*z >= World::ChunkWidth)
			{
				*currentChunk = (*currentChunk)->rightNeighbor;
				*z = *z - World::ChunkWidth;
				if (!(*currentChunk))
				{
					return false;
				}
			}

			return true;
		}

		static void updateBlockLightLevel(Chunk* originalChunk, int localX, int localY, int localZ, const glm::ivec2& chunkCoordinates, bool zeroOut, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate)
		{
			if (ChunkManager::doStepLogic && !zeroOut)
			{
				return;
			}

			if (localX >= World::ChunkDepth || localX < 0 || localZ >= World::ChunkWidth || localZ < 0)
			{
				if (!checkPositionInBounds(&originalChunk, &localX, &localZ))
				{
					g_logger_warning("Position totally out of bounds...");
					return;
				}
			}

			if (localY >= World::ChunkHeight || localY < 0)
			{
				return;
			}

			if (!originalChunk->data[to1DArray(localX, localY, localZ)].isTransparent())
			{
				return;
			}

			Chunk* currentChunk = originalChunk;
			glm::ivec3 backPropagateBlock = glm::ivec3(INT32_MIN, INT32_MIN, INT32_MIN);
			auto blocksAlreadyChecked = robin_hood::unordered_flat_set<glm::ivec3>();
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

				currentChunk = originalChunk;
				if (!checkPositionInBounds(&currentChunk, &x, &z))
				{
					g_logger_warning("Out of bounds block...");
					continue;
				}

				int arrayExpansion = to1DArray(x, y, z);
				if (!currentChunk->data[arrayExpansion].isTransparent())
				{
					continue;
				}

				Block topNeighbor = getBlockInternal(currentChunk, x, y + 1, z);
				int topNeighborLight = topNeighbor.calculatedLightLevel();
				Block bottomNeighbor = getBlockInternal(currentChunk, x, y - 1, z);
				int bottomNeighborLight = bottomNeighbor.calculatedLightLevel();
				Block leftNeighbor = getBlockInternal(currentChunk, x, y, z - 1);
				int leftNeighborLight = leftNeighbor.calculatedLightLevel();
				Block rightNeighbor = getBlockInternal(currentChunk, x, y, z + 1);
				int rightNeighborLight = rightNeighbor.calculatedLightLevel();
				Block frontNeighbor = getBlockInternal(currentChunk, x + 1, y, z);
				int frontNeighborLight = frontNeighbor.calculatedLightLevel();
				Block backNeighbor = getBlockInternal(currentChunk, x - 1, y, z);
				int backNeighborLight = backNeighbor.calculatedLightLevel();

				bool thisLightLevelChanged = false;
				int newLightLevel;
				// If the 32 bit is set in the block above, this is a sky block
				// If the top neighbor is a skyblock and this block is air, set this to a skyblock too
				if ((topNeighbor.lightLevel & 32) && currentChunk->data[arrayExpansion] == BlockMap::AIR_BLOCK)
				{
					newLightLevel = 31 | 32;
				}
				// Else If this block is a light source and it's not air, use the block format's base light source level for it
				else if (currentChunk->data[arrayExpansion] != BlockMap::AIR_BLOCK && currentChunk->data[arrayExpansion].isLightSource())
				{
					newLightLevel = BlockMap::getBlock(currentChunk->data[arrayExpansion].id).lightLevel;
				}
				// Otherwise we take the max of our surrounding light levels minus one
				else
				{
					newLightLevel =
						glm::max(glm::max(leftNeighborLight,
							glm::max(rightNeighborLight,
								glm::max(topNeighborLight,
									glm::max(bottomNeighborLight,
										glm::max(frontNeighborLight, backNeighborLight)
									)
								)
							)
						) - 1, 0);
				}

				// Check if the light level has changed, if it hasn't add the block to our back-propagation blocks
				// if we're currently zeroing out
				if (currentChunk->data[arrayExpansion].lightLevel != newLightLevel)
				{
					// If we're zeroing out and this block is not a sky block, zero the light level
					// otherwise set the light level to the new light level
					if (zeroOut && (newLightLevel & 32) != 32)
					{
						currentChunk->data[arrayExpansion].lightLevel = 0;
					}
					else
					{
						currentChunk->data[arrayExpansion].lightLevel = newLightLevel;
					}
					thisLightLevelChanged = true;
				}
				else if (currentChunk->data[arrayExpansion].lightLevel == newLightLevel && newLightLevel != 0 && zeroOut)
				{
					// Mark the correct block to start propagating backwards from
					backPropagateBlock = blockToUpdate;
					ChunkManager::backPropagationLocation =
						glm::vec3(backPropagateBlock) +
						glm::vec3(currentChunk->chunkCoords.x * 16.0f, 0.0f, currentChunk->chunkCoords.y * 16.0f) +
						glm::vec3(0.5f, 0.5f, 0.5f);
				}

				if (thisLightLevelChanged)
				{
					chunksToRetesselate.insert(currentChunk);
					currentChunk->data[arrayExpansion].lightColor =
						((7 << 0) & 0x7) | // R
						((7 << 3) & 0x38) | // G
						((7 << 6) & 0x1C0); // B
					if (newLightLevel != 0)
					{
						// We skip a block if it's a skylight or if we've already checked it because it won't change in
						// either case

						// We have to add the coordinates local to the original chunk and not the coordinates local to the current chunk
						x = blockToUpdate.x;
						y = blockToUpdate.y;
						z = blockToUpdate.z;
						if (blocksAlreadyChecked.find(glm::ivec3(x, y + 1, z)) == blocksAlreadyChecked.end() &&
							topNeighbor == BlockMap::AIR_BLOCK)
						{
							blocksToUpdate.push(glm::ivec3(x, y + 1, z));
						}
						if (blocksAlreadyChecked.find(glm::ivec3(x, y - 1, z)) == blocksAlreadyChecked.end() &&
							bottomNeighbor == BlockMap::AIR_BLOCK)
						{
							blocksToUpdate.push(glm::ivec3(x, y - 1, z));
						}
						if (blocksAlreadyChecked.find(glm::ivec3(x, y, z - 1)) == blocksAlreadyChecked.end() &&
							leftNeighbor == BlockMap::AIR_BLOCK)
						{
							blocksToUpdate.push(glm::ivec3(x, y, z - 1));
						}
						if (blocksAlreadyChecked.find(glm::ivec3(x, y, z + 1)) == blocksAlreadyChecked.end() &&
							rightNeighbor == BlockMap::AIR_BLOCK)
						{
							blocksToUpdate.push(glm::ivec3(x, y, z + 1));
						}
						if (blocksAlreadyChecked.find(glm::ivec3(x + 1, y, z)) == blocksAlreadyChecked.end() &&
							frontNeighbor == BlockMap::AIR_BLOCK)
						{
							blocksToUpdate.push(glm::ivec3(x + 1, y, z));
						}
						if (blocksAlreadyChecked.find(glm::ivec3(x - 1, y, z)) == blocksAlreadyChecked.end() &&
							backNeighbor == BlockMap::AIR_BLOCK)
						{
							blocksToUpdate.push(glm::ivec3(x - 1, y, z));
						}
					}
				}
			}

			if (backPropagateBlock != glm::ivec3(INT32_MIN, INT32_MIN, INT32_MIN))
			{
				glm::ivec2 chunkCoords = originalChunk->chunkCoords;
				glm::vec3 worldPosition = glm::vec3(chunkCoords.x * 16.0f + backPropagateBlock.x, backPropagateBlock.y,
					chunkCoords.y * 16.0f + backPropagateBlock.z);
				glm::ivec2 chunkToUpdateCoords = World::toChunkCoords(worldPosition);
				glm::ivec3 localPosition = glm::floor(worldPosition - glm::vec3(chunkToUpdateCoords.x * 16.0f, 0.0f, chunkToUpdateCoords.y * 16.0f));
				Chunk* chunk = ChunkManager::getChunk(chunkToUpdateCoords);
				// Update all the neighbors
				updateBlockLightLevel(chunk, localPosition.x + 1, localPosition.y, localPosition.z, chunkToUpdateCoords, false, chunksToRetesselate);
				updateBlockLightLevel(chunk, localPosition.x - 1, localPosition.y, localPosition.z, chunkToUpdateCoords, false, chunksToRetesselate);
				updateBlockLightLevel(chunk, localPosition.x, localPosition.y + 1, localPosition.z, chunkToUpdateCoords, false, chunksToRetesselate);
				updateBlockLightLevel(chunk, localPosition.x, localPosition.y - 1, localPosition.z, chunkToUpdateCoords, false, chunksToRetesselate);
				updateBlockLightLevel(chunk, localPosition.x, localPosition.y, localPosition.z + 1, chunkToUpdateCoords, false, chunksToRetesselate);
				updateBlockLightLevel(chunk, localPosition.x, localPosition.y, localPosition.z - 1, chunkToUpdateCoords, false, chunksToRetesselate);
			}

			// TODO: Figure out how to retesselate the chunks only once
			// Retesselate all effected chunks
			for (auto& chunk : chunksToRetesselate)
			{
				if (chunk)
				{
					ChunkManager::queueRetesselateChunk(chunk->chunkCoords, chunk);
				}
			}
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