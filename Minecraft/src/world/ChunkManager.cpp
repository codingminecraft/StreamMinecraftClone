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

namespace Minecraft
{
	enum class CommandType : uint8
	{
		SaveBlockData = 0,
		ClientLoadChunk,
		GenerateTerrain,
		GenerateDecorations,
		CalculateLighting,
		RecalculateLighting,
		TesselateVertices
	};

	struct FillChunkCommand
	{
		// Must be at least ChunkWidth * ChunkDepth * ChunkHeight blocks available
		Chunk* chunk;
		Pool<SubChunk, World::ChunkCapacity * 16>* subChunks;
		glm::ivec2 playerPosChunkCoords;
		CommandType type;
		glm::vec3 blockThatUpdated;
		bool removedLightSource;
		void* clientChunkData;
	};

	namespace ChunkPrivate
	{
		void generateTerrain(Chunk* chunk, const glm::ivec2& chunkCoordinates, float seed, const SimplexNoise& generator);
		void generateDecorations(const glm::ivec2& lastPlayerLoadPosChunkCoords, float seed, const SimplexNoise& generator);
		// Must guarantee at least 16 sub-chunks located at this address
		void generateRenderData(Pool<SubChunk, World::ChunkCapacity * 16>* subChunks, const Chunk* chunk, const glm::ivec2& chunkCoordinates);
		void calculateLighting(const glm::ivec2& lastPlayerLoadPosChunkCoords);
		void calculateLightingUpdate(Chunk* chunk, const glm::ivec2& chunkCoordinates, const glm::vec3& blockPosition, bool removedLightSource, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate);

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

			if (a.type != CommandType::CalculateLighting && a.type != CommandType::GenerateDecorations)
			{
				// They are the same type of command, the chunk closer to the player has higher priority
				glm::ivec2 tmpA = a.playerPosChunkCoords - a.chunk->chunkCoords;
				int32 aDistanceSquared = (tmpA.x * tmpA.x) + (tmpA.y * tmpA.y);
				glm::ivec2 tmpB = b.playerPosChunkCoords - b.chunk->chunkCoords;
				int32 bDistanceSquared = (tmpB.x * tmpB.x) + (tmpB.y * tmpB.y);
				return aDistanceSquared > bDistanceSquared;
			}

			return false;
		}
	};

	namespace ChunkManager
	{
		extern bool doStepLogic = false;

		class ChunkWorker
		{
		public:
			ChunkWorker()
				: cv(), mtx(), queueMtx(), doWork(true)
			{
				noiseGenerators[0] = SimplexNoise();// World::seedAsFloat.load());
				noiseGenerators[1] = SimplexNoise(World::seedAsFloat.load());
				noiseGenerators[2] = SimplexNoise(World::seedAsFloat.load());
				noiseGenerators[3] = SimplexNoise(World::seedAsFloat.load());
				noiseGenerators[4] = SimplexNoise(World::seedAsFloat.load());

				workerThread = std::thread(&ChunkWorker::threadWorker, this);
			}

			void free()
			{
				{
					std::lock_guard lock(mtx);
					doWork = false;
				}
				cv.notify_all();

				workerThread.join();
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
							// Only process all commands if we're not stopping the thread worker.
							// If we are stopping the thread worker, then only process save commands
							do
							{
								command = commands.top();
								commands.pop();
								processCommand = (!doWork && command.type == CommandType::SaveBlockData) || doWork;
							} while (!doWork && command.type != CommandType::SaveBlockData && commands.size() > 0);
						}
					}

					if (processCommand)
					{
						switch (command.type)
						{
						case CommandType::ClientLoadChunk:
						{
							g_logger_assert(command.clientChunkData != nullptr, "Invalid client data sent to the chunk.");
							g_memory_copyMem(command.chunk->data, command.clientChunkData,
								sizeof(Block) * World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);
							g_memory_free(command.clientChunkData);
							command.chunk->needsToGenerateDecorations = false;
							command.chunk->needsToCalculateLighting = true;
							break;
						}
						case CommandType::GenerateTerrain:
						{
							if (ChunkPrivate::exists(World::chunkSavePath, command.chunk->chunkCoords))
							{
								ChunkPrivate::deserialize(command.chunk->data, World::chunkSavePath, command.chunk->chunkCoords);
								command.chunk->needsToGenerateDecorations = false;
							}
							else
							{
								ChunkPrivate::generateTerrain(command.chunk, command.chunk->chunkCoords, World::seedAsFloat, noiseGenerators[0]);
								command.chunk->needsToGenerateDecorations = true;
							}
							command.chunk->needsToCalculateLighting = true;
						}
						break;
						case CommandType::GenerateDecorations:
						{
							ChunkPrivate::generateDecorations(command.playerPosChunkCoords, World::seedAsFloat, noiseGenerators[0]);
						}
						break;
						case CommandType::CalculateLighting:
						{
							ChunkPrivate::calculateLighting(command.playerPosChunkCoords);
						}
						break;
						case CommandType::RecalculateLighting:
						{
							robin_hood::unordered_flat_set<Chunk*> chunksToRetesselate = {};
							ChunkPrivate::calculateLightingUpdate(command.chunk, command.chunk->chunkCoords, command.blockThatUpdated, command.removedLightSource, chunksToRetesselate);
							for (Chunk* chunk : chunksToRetesselate)
							{
								// TODO: I should probably do all this from within the thread...
								ChunkManager::queueRetesselateChunk(chunk->chunkCoords, chunk);
								//command.chunk = chunk;
								//command.type = CommandType::TesselateVertices;
								//queueCommand(command);
							}
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
							command.chunk->state = ChunkState::Unloading;
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
			std::thread workerThread;
			std::atomic<glm::ivec2> playerPosChunkCoords;
			std::condition_variable cv;
			std::mutex mtx;
			std::mutex queueMtx;
			bool doWork;
			std::atomic<bool> waitingOnCommand = false;

			std::array<SimplexNoise, 5> noiseGenerators;
		};

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
		static std::list<Block*> chunkFreeList = {};

		static uint32 chunkPosInstancedBuffer;
		static uint32 biomeInstancedVbo;
		static uint32 globalVao;
		static uint32 globalRenderVbo;
		// TODO: Make this better
		static uint32 solidDrawCommandVbo;
		static uint32 blendableDrawCommandVbo;
		static Shader compositeShader;

		static ChunkWorker* chunkWorker = nullptr;
		static Pool<SubChunk, World::ChunkCapacity * 16>* subChunks = nullptr;
		static Pool<Block, World::ChunkCapacity>* blockPool = nullptr;
		static CommandBufferContainer* solidCommandBuffer = nullptr;
		static CommandBufferContainer* blendableCommandBuffer = nullptr;

		void init()
		{
			// A chunk uses 55,000 vertices on average, so a sub-chunk can be estimated to use about 
			// 4,500 vertices on average. That's the default vertex bucket size
			processorCount = 1;// std::thread::hardware_concurrency();

			// Initialize the singletons
			chunkWorker = new ChunkWorker();
			subChunks = new Pool<SubChunk, World::ChunkCapacity * 16>(1);
			blockPool = new Pool<Block, World::ChunkCapacity>(World::ChunkDepth * World::ChunkWidth * World::ChunkHeight);
			solidCommandBuffer = new CommandBufferContainer(subChunks->size(), false);
			blendableCommandBuffer = new CommandBufferContainer(subChunks->size(), true);

			compositeShader.compile("assets/shaders/CompositeShader.glsl");

			// Initialize the free list
			chunks.clear();
			chunkFreeList.clear();
			for (int i = 0; i < (int)blockPool->size(); i++)
			{
				chunkFreeList.push_back((*blockPool)[i]);
			}

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
			chunkFreeList.clear();

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
				if (chunk.state != ChunkState::Saving && blockData)
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
					cmd.subChunks = subChunks;

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

		void queueCalculateLighting(const glm::ivec2& lastPlayerPosInChunkCoords)
		{
			FillChunkCommand cmd;
			cmd.type = CommandType::CalculateLighting;
			cmd.playerPosChunkCoords = lastPlayerPosInChunkCoords;
			chunkWorker->queueCommand(cmd);
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
				// TODO: this probably isn't necessary, find all the unneccessary retesselations and remove them
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
				cmd.subChunks = subChunks;
				cmd.chunk = chunk;

				// Update the sub-chunks that are about to be deleted
				for (int i = 0; i < (int)subChunks->size(); i++)
				{
					if ((*subChunks)[i]->chunkCoordinates == chunkCoordinates && (*subChunks)[i]->state == SubChunkState::Uploaded)
					{
						(*subChunks)[i]->state = SubChunkState::RetesselateVertices;
					}
				}

				// TODO: Remove this flag, this is mostly for debugging
				if (!doImmediately)
				{
					chunkWorker->queueCommand(cmd);
					chunkWorker->beginWork();
				}
				else
				{
					ChunkPrivate::generateRenderData(subChunks, chunk, chunk->chunkCoords);
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

		void beginWork()
		{
			chunkWorker->beginWork();
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

						if ((*subChunks)[i]->state == SubChunkState::Uploaded || (*subChunks)[i]->state == SubChunkState::RetesselateVertices ||
							(*subChunks)[i]->state == SubChunkState::DoneRetesselating)
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

		void checkChunkRadius(const glm::vec3& playerPosition)
		{
			glm::ivec2 playerPosChunkCoords = World::toChunkCoords(playerPosition);
			chunkWorker->setPlayerPosChunkCoords(playerPosChunkCoords);
			static glm::ivec2 lastPlayerPosChunkCoords = playerPosChunkCoords;

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
						auto& iter = chunks.find(chunkPos);
						if (iter != chunks.end() && iter->second.state != ChunkState::Saving)
						{
							if (iter->second.state != ChunkState::Saving)
							{
								queueSaveChunk((*subChunks)[i]->chunkCoordinates);
							}
						}
					}
				}
			}

			// Unload any chunks that have been deserialized
			for (auto iter = chunks.begin(); iter != chunks.end();)
			{
				if (iter->second.state == ChunkState::Unloading)
				{
					DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed - (float)(blockPool->poolSize() * sizeof(Block));

					chunkFreeList.push_back(iter->second.data);
					iter = chunks.erase(iter);
				}
				else
				{
					iter++;
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
			ChunkManager::queueCalculateLighting(playerPosChunkCoords);
			lastPlayerPosChunkCoords = playerPosChunkCoords;

			ChunkManager::patchChunkPointers();
			if (needsWork)
			{
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
		static const int SKY_LIGHT_LEVEL_BITMASK = 0x3e0000;

		static const int BASE_17_DEPTH = 17;
		static const int BASE_17_WIDTH = 17;
		static const int BASE_17_HEIGHT = 289;

		// Internal functions
		static int to1DArray(int x, int y, int z);
		static Block getBlockInternal(const Chunk* chunk, int x, int y, int z);
		static bool setBlockInternal(Chunk* chunk, int x, int y, int z, Block newBlock);
		static bool removeBlockInternal(Chunk* chunk, int x, int y, int z);
		static std::string getFormattedFilepath(const glm::ivec2& chunkCoordinates, const std::string& worldSavePath);
		static void loadBlock(Vertex* vertexData, const glm::ivec3& vert1, const glm::ivec3& vert2, const glm::ivec3& vert3, const glm::ivec3& vert4, const TextureFormat& texture, CUBE_FACE face, bool colorFaceBasedOnBiome, int lightLevel, const glm::ivec3& lightColor, int skyLightLevel);
		static void calculateNextLightLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck);
		static void removeNextLightLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck, std::queue<glm::ivec3>& lightSources, bool ignoreThisSolidBlock);
		// TODO: Consider removing this duplication if it doesn't effect performance
		static void calculateNextSkyLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck);
		static void removeNextSkyLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck, std::queue<glm::ivec3>& lightSources, bool ignoreThisSolidBlock);

		void info()
		{
			g_logger_info("%d size of chunk", sizeof(Block) * World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);
			g_logger_info("Max %d size of vertex data", sizeof(Vertex) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth * 24);
		}

		const float maxBiomeHeight = 145.0f;
		const float minBiomeHeight = 55.0f;
		const int oceanLevel = 85;
		void generateTerrain(Chunk* chunk, const glm::ivec2& chunkCoordinates, float seed, const SimplexNoise& generator)
		{
			const int worldChunkX = chunkCoordinates.x * 16;
			const int worldChunkZ = chunkCoordinates.y * 16;

			g_memory_zeroMem(chunk->data, sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth);
			for (int x = 0; x < World::ChunkDepth; x++)
			{
				for (int z = 0; z < World::ChunkWidth; z++)
				{
					int16 maxHeight = TerrainGenerator::getHeight(generator, x + worldChunkX, z + worldChunkZ, minBiomeHeight, maxBiomeHeight);
					int16 stoneHeight = (int16)(maxHeight - 3.0f);

					for (int y = 0; y < World::ChunkHeight; y++)
					{
						const int arrayExpansion = to1DArray(x, y, z);
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
							if (maxHeight < oceanLevel + 2)
							{
								// Sand
								chunk->data[arrayExpansion].id = 3;
							}
							else
							{
								// Grass
								chunk->data[arrayExpansion].id = 2;
							}
						}
						else if (y >= minBiomeHeight && y < oceanLevel)
						{
							// Water 
							chunk->data[arrayExpansion].id = 19;
						}
						else if (!chunk->data[arrayExpansion].id)
						{
							chunk->data[arrayExpansion].id = BlockMap::AIR_BLOCK.id;
						}
					}
				}
			}
		}

		void generateDecorations(const glm::ivec2& lastPlayerLoadPosChunkCoords, float seed, const SimplexNoise& generator)
		{
			for (int chunkZ = lastPlayerLoadPosChunkCoords.y - World::ChunkRadius; chunkZ <= lastPlayerLoadPosChunkCoords.y + World::ChunkRadius; chunkZ++)
			{
				for (int chunkX = lastPlayerLoadPosChunkCoords.x - World::ChunkRadius; chunkX <= lastPlayerLoadPosChunkCoords.x + World::ChunkRadius; chunkX++)
				{
					const int worldChunkX = chunkX * 16;
					const int worldChunkZ = chunkZ * 16;

					glm::ivec2 localChunkPos = glm::vec2(lastPlayerLoadPosChunkCoords.x - chunkX, lastPlayerLoadPosChunkCoords.y - chunkZ);
					bool inRangeOfPlayer =
						(localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) <=
						((World::ChunkRadius - 1) * (World::ChunkRadius - 1));
					if (!inRangeOfPlayer)
					{
						// Skip over all chunks in range radius - 1
						continue;
					}

					Chunk* chunk = ChunkManager::getChunk(glm::vec3(worldChunkX, 128.0f, worldChunkZ));
					if (!chunk)
					{
						// TODO: Is this a problem...? It should only effect chunks on the edge of the border
						// g_logger_error("Bad chunk when generating terrain. Skipping chunk.");
						continue;
					}

					if (!chunk->needsToGenerateDecorations)
					{
						continue;
					}
					chunk->needsToGenerateDecorations = false;

					for (int x = 0; x < World::ChunkDepth; x++)
					{
						for (int z = 0; z < World::ChunkWidth; z++)
						{
							// Generate some trees if needed
							int num = (rand() % 100);
							bool generateTree = num > 98;

							if (generateTree)
							{
								int16 y = TerrainGenerator::getHeight(generator, x + worldChunkX, z + worldChunkZ, minBiomeHeight, maxBiomeHeight) + 1;

								if (y > oceanLevel + 2)
								{
									// Generate a tree
									int treeHeight = (rand() % 3) + 3;
									int leavesBottomY = glm::clamp(treeHeight - 3, 3, (int)World::ChunkHeight - 1);
									int leavesTopY = treeHeight + 1;
									if (generateTree && (y + 1 + leavesTopY < World::ChunkHeight))
									{
										for (int treeY = 0; treeY <= treeHeight; treeY++)
										{
											chunk->data[to1DArray(x, treeY + y, z)].id = 8;
										}

										int ringLevel = 0;
										for (int leavesY = leavesBottomY + y; leavesY <= leavesTopY + y; leavesY++)
										{
											int leafRadius = leavesY == leavesTopY ? 2 : 1;
											for (int leavesX = x - leafRadius; leavesX <= x + leafRadius; leavesX++)
											{
												for (int leavesZ = z - leafRadius; leavesZ <= z + leafRadius; leavesZ++)
												{
													if (leavesX < World::ChunkDepth && leavesX >= 0 && leavesZ < World::ChunkWidth && leavesZ >= 0)
													{
														chunk->data[to1DArray(leavesX, leavesY, leavesZ)].id = 9;
													}
													else if (leavesX < 0)
													{
														if (chunk->bottomNeighbor)
														{
															chunk->bottomNeighbor->data[to1DArray(World::ChunkDepth + leavesX, leavesY, leavesZ)].id = 9;
														}
													}
													else if (leavesX >= World::ChunkDepth)
													{
														if (chunk->topNeighbor)
														{
															chunk->topNeighbor->data[to1DArray(leavesX - World::ChunkDepth, leavesY, leavesZ)].id = 9;
														}
													}
													else if (leavesZ < 0)
													{
														if (chunk->leftNeighbor)
														{
															chunk->leftNeighbor->data[to1DArray(leavesX, leavesY, World::ChunkWidth + leavesZ)].id = 9;
														}
													}
													else if (leavesZ >= World::ChunkWidth)
													{
														if (chunk->rightNeighbor)
														{
															chunk->rightNeighbor->data[to1DArray(leavesX, leavesY, leavesZ - World::ChunkWidth)].id = 9;
														}
													}
												}
											}
											ringLevel++;
										}
									}
								}
							}
						}
					}
				}
			}
		}

		static void calculateChunkLighting(Chunk* chunk, const glm::ivec2& chunkCoordinates);
		static void calculateChunkSkyBlocks(Chunk* chunk, const glm::ivec2& chunkCoordinates);
		void calculateLighting(const glm::ivec2& lastPlayerLoadPosChunkCoords)
		{
			for (int i = 0; i < 2; i++)
			{
				for (int chunkZ = lastPlayerLoadPosChunkCoords.y - World::ChunkRadius; chunkZ <= lastPlayerLoadPosChunkCoords.y + World::ChunkRadius; chunkZ++)
				{
					for (int chunkX = lastPlayerLoadPosChunkCoords.x - World::ChunkRadius; chunkX <= lastPlayerLoadPosChunkCoords.x + World::ChunkRadius; chunkX++)
					{
						const int worldChunkX = chunkX * 16;
						const int worldChunkZ = chunkZ * 16;

						glm::ivec2 localChunkPos = glm::vec2(lastPlayerLoadPosChunkCoords.x - chunkX, lastPlayerLoadPosChunkCoords.y - chunkZ);
						bool inRangeOfPlayer =
							(localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) <=
							((World::ChunkRadius - 1) * (World::ChunkRadius - 1));
						if (!inRangeOfPlayer)
						{
							// Skip over all chunks in range radius - 1
							continue;
						}

						Chunk* chunk = ChunkManager::getChunk(glm::vec3(worldChunkX, 128.0f, worldChunkZ));
						if (!chunk)
						{
							// TODO: Is this a problem...? It should only effect chunks on the edge of the border
							// g_logger_error("Bad chunk when generating terrain. Skipping chunk.");
							continue;
						}

						if (!chunk->needsToCalculateLighting)
						{
							continue;
						}

						if (i == 0)
						{
							// First iteration calculate all sky light levels
							calculateChunkSkyBlocks(chunk, glm::ivec2(chunkX, chunkZ));
						}
						else if (i == 1)
						{
							// Second iteration calculate all sky "sources" and light sources
							calculateChunkLighting(chunk, glm::ivec2(chunkX, chunkZ));
							chunk->needsToCalculateLighting = false;
						}
					}
				}
			}
		}

		static void calculateChunkSkyBlocks(Chunk* chunk, const glm::ivec2& chunkCoordinates)
		{
			for (int x = 0; x < World::ChunkDepth; x++)
			{
				for (int z = 0; z < World::ChunkWidth; z++)
				{
					for (int y = World::ChunkHeight - 1; y >= 0; y--)
					{
						int arrayExpansion = to1DArray(x, y, z);
						if (!chunk->data[arrayExpansion].isTransparent())
						{
							// We're done propagating here
							break;
						}

						// Set the block to the max light level since this has to be a sky block
						chunk->data[arrayExpansion].setSkyLightLevel(31);
						chunk->data[arrayExpansion].lightColor =
							((7 << 0) & 0x7) |  // R
							((7 << 3) & 0x38) | // G
							((7 << 6) & 0x1C0); // B
					}
				}
			}
		}

		static void calculateChunkLighting(Chunk* chunk, const glm::ivec2& chunkCoordinates)
		{
			// Propagate any sky blocks that are acting like "sources"
			bool anySkySources = false;
			std::queue<glm::ivec3> skyBlocksToUpdate = {};
			for (int y = World::ChunkHeight - 1; y >= 0; y--)
			{
				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						int arrayExpansion = to1DArray(x, y, z);
						if (!chunk->data[arrayExpansion].isTransparent())
						{
							continue;
						}

						if (chunk->data[arrayExpansion].calculatedSkyLightLevel() == 31)
						{
							anySkySources = true;

							// If any of the horizontal neighbors is transparent and not a sky block, add this block
							// as a source
							for (int i = 0; i < INormals3::CardinalDirections.size(); i++)
							{
								if (INormals3::CardinalDirections[i].y == 0)
								{
									glm::ivec3 blockLocalPos = glm::ivec3(x, y, z) + INormals3::CardinalDirections[i];
									Block block = getBlockInternal(chunk, blockLocalPos.x, blockLocalPos.y, blockLocalPos.z);
									if (block.calculatedSkyLightLevel() != 31 && block.isTransparent())
									{
										skyBlocksToUpdate.push({ x, y, z });
										break;
									}
								}
							}
						}
					}
				}

				if (!anySkySources)
				{
					// If this horizontal slice of the world had no sky sources, we are done
					// checking all potential light sources
					break;
				}
			}
			robin_hood::unordered_flat_set<Chunk*> skyChunksToRetesselate = {};
			while (!skyBlocksToUpdate.empty())
			{
				calculateNextSkyLevel(chunk, chunkCoordinates, skyChunksToRetesselate, skyBlocksToUpdate);
			}

			// Then calculate all light sources
			std::queue<glm::ivec3> blocksToUpdate = {};
			for (int y = 0; y < World::ChunkHeight; y++)
			{
				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						int arrayExpansion = to1DArray(x, y, z);
						if (!chunk->data[arrayExpansion].isLightSource())
						{
							continue;
						}
						chunk->data[arrayExpansion].lightLevel = BlockMap::getBlock(chunk->data[arrayExpansion].id).lightLevel;
						blocksToUpdate.push({ x, y, z });
					}
				}
			}

			robin_hood::unordered_flat_set<Chunk*> chunksToRetesselate = {};
			while (!blocksToUpdate.empty())
			{
				calculateNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
			}
		}

		void calculateLightingUpdate(Chunk* chunk, const glm::ivec2& chunkCoordinates, const glm::vec3& blockPosition, bool removedLightSource, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate)
		{
			glm::ivec3 localPosition = glm::floor(blockPosition - glm::vec3(chunkCoordinates.x * 16.0f, 0.0f, chunkCoordinates.y * 16.0f));
			int localX = localPosition.x;
			int localY = localPosition.y;
			int localZ = localPosition.z;
			Block blockThatsUpdating = chunk->data[to1DArray(localX, localY, localZ)];
			if (!blockThatsUpdating.isTransparent() && !blockThatsUpdating.isLightSource() && !removedLightSource)
			{
				// Just placed a solid block
				std::queue<glm::ivec3> blocksToZero = {};
				std::queue<glm::ivec3> blocksToUpdate = {};
				blocksToZero.push({ localX, localY, localZ });
				bool ignoreThisSolidBlock = true;
				// Zero out
				while (!blocksToZero.empty())
				{
					removeNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToZero, blocksToUpdate, ignoreThisSolidBlock);
					ignoreThisSolidBlock = false;
				}

				// Flood fill from all the light sources that were found
				while (!blocksToUpdate.empty())
				{
					calculateNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
				}

				blocksToZero.push({ localX, localY, localZ });
				ignoreThisSolidBlock = true;
				// Zero out
				while (!blocksToZero.empty())
				{
					removeNextSkyLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToZero, blocksToUpdate, ignoreThisSolidBlock);
					ignoreThisSolidBlock = false;
				}

				// Flood fill from all the light sources that were found
				while (!blocksToUpdate.empty())
				{
					calculateNextSkyLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
				}
			}
			else if (removedLightSource)
			{
				// Just removed a light source
				std::queue<glm::ivec3> blocksToZero = {};
				std::queue<glm::ivec3> blocksToUpdate = {};
				blocksToZero.push({ localX, localY, localZ });
				// Zero out
				while (!blocksToZero.empty())
				{
					removeNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToZero, blocksToUpdate, false);
				}

				// Flood fill from all the light sources that were found
				while (!blocksToUpdate.empty())
				{
					calculateNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
				}
			}
			else if (blockThatsUpdating.isLightSource())
			{
				// Just added a light source
				std::queue<glm::ivec3> blocksToUpdate = {};
				blocksToUpdate.push({ localX, localY, localZ });
				int arrayExpansion = to1DArray(localX, localY, localZ);
				chunk->data[arrayExpansion].setLightLevel(BlockMap::getBlock(chunk->data[arrayExpansion].id).lightLevel);
				while (!blocksToUpdate.empty())
				{
					calculateNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
				}
			}
			else
			{
				// Just removed a block
				std::queue<glm::ivec3> blocksToUpdate = {};
				blocksToUpdate.push({ localX, localY, localZ });
				int arrayExpansion = to1DArray(localX, localY, localZ);

				// My light level is now the max of all my neighbors minus one
				int myLightLevel = 0;
				int mySkyLevel = 0;
				for (int i = 0; i < INormals3::CardinalDirections.size(); i++)
				{
					const glm::ivec3& normal = INormals3::CardinalDirections[i];
					Block block = getBlockInternal(chunk, localX + normal.x, localY + normal.y, localZ + normal.z);
					if ((block.calculatedLightLevel() - 1) > myLightLevel)
					{
						myLightLevel = block.calculatedLightLevel() - 1;
					}
					if ((block.calculatedSkyLightLevel() - 1) > mySkyLevel)
					{
						mySkyLevel = block.calculatedSkyLightLevel() - 1;
					}
					// If the block above me is a sky block, I am also a sky block
					if (block.calculatedSkyLightLevel() == 31 && normal.y == 1)
					{
						mySkyLevel = 31;
					}
				}
				chunk->data[arrayExpansion].setLightLevel(myLightLevel);
				while (!blocksToUpdate.empty())
				{
					calculateNextLightLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
				}

				chunk->data[arrayExpansion].setSkyLightLevel(mySkyLevel);
				blocksToUpdate.push({ localX, localY, localZ });
				// If I was a sky block, set all transparent blocks below me to sky blocks
				if (mySkyLevel == 31)
				{
					for (int y = localY; y >= 0; y--)
					{
						int otherBlockArrayExpansion = to1DArray(localX, y, localZ);
						Block otherBlock = chunk->data[otherBlockArrayExpansion];
						if (!otherBlock.isTransparent())
						{
							break;
						}

						chunk->data[arrayExpansion].setSkyLightLevel(31);
						blocksToUpdate.push({ localX, y, localZ });
					}
				}

				while (!blocksToUpdate.empty())
				{
					calculateNextSkyLevel(chunk, chunkCoordinates, chunksToRetesselate, blocksToUpdate);
				}
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

		static SubChunk* getSubChunk(Pool<SubChunk, World::ChunkCapacity * 16>* subChunks, SubChunk* currentSubChunk, int currentLevel, const glm::ivec2& chunkCoordinates, bool isBlendableSubChunk)
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
					ret->isBlendable = isBlendableSubChunk;
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

			SubChunk* solidSubChunk = nullptr;
			SubChunk* blendableSubChunk = nullptr;
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
						bool currentBlockIsBlendable = blockFormat.isBlendable;
						bool currentBlockIsTransparent = blockFormat.isTransparent;
						bool currentBlockIsWater = blockId == 19;

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
						}
						const BlockFormat* blockFormats[6] = {
							&BlockMap::getBlock(blocks[0].id),
							&BlockMap::getBlock(blocks[1].id),
							&BlockMap::getBlock(blocks[2].id),
							&BlockMap::getBlock(blocks[3].id),
							&BlockMap::getBlock(blocks[4].id),
							&BlockMap::getBlock(blocks[5].id)
						};

						SubChunk** currentSubChunkPtr = &solidSubChunk;
						if (currentBlockIsBlendable)
						{
							currentSubChunkPtr = &blendableSubChunk;
						}

						// Only add the faces that are not culled by other blocks
						for (int i = 0; i < 6; i++)
						{
							if (blocks[i].id && (blockFormats[i]->isTransparent && !currentBlockIsWater) || (blocks[i] == BlockMap::AIR_BLOCK && currentBlockIsWater))
							{
								*currentSubChunkPtr = getSubChunk(subChunks, *currentSubChunkPtr, currentLevel, chunkCoordinates, currentBlockIsBlendable);
								SubChunk* currentSubChunk = *currentSubChunkPtr;
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
									blocks[i].calculatedLightLevel(),
									lightColors[i],
									blocks[i].calculatedSkyLightLevel());
								currentSubChunk->numVertsUsed += 6;
							}
						}
					}
				}
			}

			if (solidSubChunk && solidSubChunk->numVertsUsed > 0)
			{
				solidSubChunk->state = SubChunkState::UploadVerticesToGpu;
			}

			if (blendableSubChunk && blendableSubChunk->numVertsUsed > 0)
			{
				blendableSubChunk->state = SubChunkState::UploadVerticesToGpu;
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
			if (!Network::isNetworkEnabled())
			{
				std::string filepath = getFormattedFilepath(chunkCoordinates, worldSavePath);
				FILE* fp = fopen(filepath.c_str(), "rb");
				if (!fp)
				{
					g_logger_error("Could not open file '%s'", filepath.c_str());
					return;
				}

				fread(blockData, sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth, 1, fp);
				// TODO: Separate lightmaps into different arrays than block ids
				for (int y = 0; y < World::ChunkHeight; y++)
				{
					for (int x = 0; x < World::ChunkDepth; x++)
					{
						for (int z = 0; z < World::ChunkWidth; z++)
						{
							blockData[to1DArray(x, y, z)].setLightLevel(0);
							blockData[to1DArray(x, y, z)].setSkyLightLevel(0);
						}
					}
				}
				fclose(fp);
			}
			else
			{
				g_logger_warning("Cannot deserialize chunk over the network yet...");
			}
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
			chunk->data[index].id = newBlock.id;

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
			chunk->data[index].id = BlockMap::AIR_BLOCK.id;
			chunk->data[index].lightColor =
				((7 << 0) & 0x7) | // R
				((7 << 3) & 0x38) | // G
				((7 << 6) & 0x1C0); // B

			return true;
		}

		static bool checkPositionInBounds(Chunk** currentChunk, int* x, int y, int* z)
		{
			if (y < 0 || y >= World::ChunkHeight)
			{
				return false;
			}

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

		static void calculateNextLightLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck)
		{
			glm::ivec3 blockToUpdate = blocksToCheck.front();
			blocksToCheck.pop();

			if (!originalChunk)
			{
				g_logger_warning("Encountered weird null chunk while updating block lighting.");
				return;
			}

			int blockToUpdateX = blockToUpdate.x;
			int blockToUpdateY = blockToUpdate.y;
			int blockToUpdateZ = blockToUpdate.z;
			Chunk* blockToUpdateChunk = originalChunk;
			if (blockToUpdateX >= World::ChunkDepth || blockToUpdateX < 0 || blockToUpdateZ >= World::ChunkWidth || blockToUpdateZ < 0)
			{
				if (!checkPositionInBounds(&blockToUpdateChunk, &blockToUpdateX, blockToUpdateY, &blockToUpdateZ))
				{
					g_logger_warning("Position totally out of bounds...");
					return;
				}
				chunksToRetesselate.insert(blockToUpdateChunk);
			}

			int arrayExpansion = to1DArray(blockToUpdateX, blockToUpdateY, blockToUpdateZ);
			if (!blockToUpdateChunk->data[arrayExpansion].isTransparent() &&
				!blockToUpdateChunk->data[arrayExpansion].isLightSource())
			{
				return;
			}

			int myLightLevel = blockToUpdateChunk->data[arrayExpansion].calculatedLightLevel();
			if (myLightLevel > 0)
			{
				for (int i = 0; i < INormals3::CardinalDirections.size(); i++)
				{
					const glm::ivec3& iNormal = INormals3::CardinalDirections[i];
					const glm::ivec3 pos = glm::ivec3(blockToUpdateX + iNormal.x, blockToUpdateY + iNormal.y, blockToUpdateZ + iNormal.z);
					Block neighbor = getBlockInternal(blockToUpdateChunk, pos.x, pos.y, pos.z);
					int neighborLight = neighbor.calculatedLightLevel();
					if (neighborLight <= myLightLevel - 2 && neighbor.isTransparent())
					{
						Chunk* neighborChunk = blockToUpdateChunk;
						int neighborLocalX = pos.x;
						int neighborLocalZ = pos.z;
						if (checkPositionInBounds(&neighborChunk, &neighborLocalX, pos.y, &neighborLocalZ))
						{
							neighborChunk->data[to1DArray(neighborLocalX, pos.y, neighborLocalZ)].setLightLevel(myLightLevel - 1);
							blocksToCheck.push(glm::ivec3(blockToUpdate.x + iNormal.x, blockToUpdate.y + iNormal.y, blockToUpdate.z + iNormal.z));
							chunksToRetesselate.insert(neighborChunk);
						}
					}
				}
			}
		}

		static void removeNextLightLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck, std::queue<glm::ivec3>& lightSources, bool ignoreThisSolidBlock)
		{
			glm::ivec3 blockToUpdate = blocksToCheck.front();
			blocksToCheck.pop();

			if (!originalChunk)
			{
				g_logger_warning("Encountered weird null chunk while updating block lighting.");
				return;
			}

			int blockToUpdateX = blockToUpdate.x;
			int blockToUpdateY = blockToUpdate.y;
			int blockToUpdateZ = blockToUpdate.z;
			Chunk* blockToUpdateChunk = originalChunk;
			if (blockToUpdateX >= World::ChunkDepth || blockToUpdateX < 0 || blockToUpdateZ >= World::ChunkWidth || blockToUpdateZ < 0)
			{
				if (!checkPositionInBounds(&blockToUpdateChunk, &blockToUpdateX, blockToUpdateY, &blockToUpdateZ))
				{
					g_logger_warning("Position totally out of bounds...");
					return;
				}
				chunksToRetesselate.insert(blockToUpdateChunk);
			}

			int arrayExpansion = to1DArray(blockToUpdateX, blockToUpdateY, blockToUpdateZ);
			if (!ignoreThisSolidBlock &&
				!blockToUpdateChunk->data[arrayExpansion].isTransparent() &&
				!blockToUpdateChunk->data[arrayExpansion].isLightSource())
			{
				return;
			}

			int myOldLightLevel = blockToUpdateChunk->data[arrayExpansion].calculatedLightLevel();
			blockToUpdateChunk->data[arrayExpansion].setLightLevel(0);
			for (int i = 0; i < INormals3::CardinalDirections.size(); i++)
			{
				const glm::ivec3& iNormal = INormals3::CardinalDirections[i];
				const glm::ivec3 pos = glm::ivec3(blockToUpdateX + iNormal.x, blockToUpdateY + iNormal.y, blockToUpdateZ + iNormal.z);
				Block neighbor = getBlockInternal(blockToUpdateChunk, pos.x, pos.y, pos.z);
				int neighborLight = neighbor.calculatedLightLevel();
				if (neighborLight != 0 && neighborLight < myOldLightLevel && neighbor.isTransparent())
				{
					Chunk* neighborChunk = blockToUpdateChunk;
					int neighborLocalX = pos.x;
					int neighborLocalZ = pos.z;
					if (checkPositionInBounds(&neighborChunk, &neighborLocalX, pos.y, &neighborLocalZ))
					{
						blocksToCheck.push(glm::ivec3(blockToUpdate.x + iNormal.x, blockToUpdate.y + iNormal.y, blockToUpdate.z + iNormal.z));
						chunksToRetesselate.insert(neighborChunk);
					}
				}
				else if (neighborLight > myOldLightLevel)
				{
					Chunk* neighborChunk = blockToUpdateChunk;
					int neighborLocalX = pos.x;
					int neighborLocalZ = pos.z;
					if (checkPositionInBounds(&neighborChunk, &neighborLocalX, pos.y, &neighborLocalZ))
					{
						lightSources.push(glm::ivec3(blockToUpdate.x + iNormal.x, blockToUpdate.y + iNormal.y, blockToUpdate.z + iNormal.z));
						chunksToRetesselate.insert(neighborChunk);
					}
				}
			}
		}

		// TODO: Think about removing this duplication if it doesn't effect performance
		static void calculateNextSkyLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck)
		{
			glm::ivec3 blockToUpdate = blocksToCheck.front();
			blocksToCheck.pop();

			if (!originalChunk)
			{
				g_logger_warning("Encountered weird null chunk while updating block lighting.");
				return;
			}

			int blockToUpdateX = blockToUpdate.x;
			int blockToUpdateY = blockToUpdate.y;
			int blockToUpdateZ = blockToUpdate.z;
			Chunk* blockToUpdateChunk = originalChunk;
			if (blockToUpdateX >= World::ChunkDepth || blockToUpdateX < 0 || blockToUpdateZ >= World::ChunkWidth || blockToUpdateZ < 0)
			{
				if (!checkPositionInBounds(&blockToUpdateChunk, &blockToUpdateX, blockToUpdateY, &blockToUpdateZ))
				{
					return;
				}
				chunksToRetesselate.insert(blockToUpdateChunk);
			}

			int arrayExpansion = to1DArray(blockToUpdateX, blockToUpdateY, blockToUpdateZ);
			if (!blockToUpdateChunk->data[arrayExpansion].isTransparent())
			{
				return;
			}

			int myLightLevel = blockToUpdateChunk->data[arrayExpansion].calculatedSkyLightLevel();
			if (myLightLevel > 0)
			{
				for (int i = 0; i < INormals3::CardinalDirections.size(); i++)
				{
					const glm::ivec3& iNormal = INormals3::CardinalDirections[i];
					const glm::ivec3 pos = glm::ivec3(blockToUpdateX + iNormal.x, blockToUpdateY + iNormal.y, blockToUpdateZ + iNormal.z);
					Block neighbor = getBlockInternal(blockToUpdateChunk, pos.x, pos.y, pos.z);
					int neighborLight = neighbor.calculatedSkyLightLevel();
					if (neighborLight <= myLightLevel - 2 && neighbor.isTransparent())
					{
						Chunk* neighborChunk = blockToUpdateChunk;
						int neighborLocalX = pos.x;
						int neighborLocalZ = pos.z;
						if (checkPositionInBounds(&neighborChunk, &neighborLocalX, pos.y, &neighborLocalZ))
						{
							neighborChunk->data[to1DArray(neighborLocalX, pos.y, neighborLocalZ)].setSkyLightLevel(myLightLevel - 1);
							blocksToCheck.push(glm::ivec3(blockToUpdate.x + iNormal.x, blockToUpdate.y + iNormal.y, blockToUpdate.z + iNormal.z));
							//g_logger_assert(iNormal.y != 1, "Sky sources should never propagate up once we get inside of here.");
							chunksToRetesselate.insert(neighborChunk);
						}
					}
				}
			}
		}

		static void removeNextSkyLevel(Chunk* originalChunk, const glm::ivec2& chunkCoordinates, robin_hood::unordered_flat_set<Chunk*>& chunksToRetesselate, std::queue<glm::ivec3>& blocksToCheck, std::queue<glm::ivec3>& lightSources, bool ignoreThisSolidBlock)
		{
			glm::ivec3 blockToUpdate = blocksToCheck.front();
			blocksToCheck.pop();

			if (!originalChunk)
			{
				g_logger_warning("Encountered weird null chunk while updating block lighting.");
				return;
			}

			int blockToUpdateX = blockToUpdate.x;
			int blockToUpdateY = blockToUpdate.y;
			int blockToUpdateZ = blockToUpdate.z;
			Chunk* blockToUpdateChunk = originalChunk;
			if (blockToUpdateX >= World::ChunkDepth || blockToUpdateX < 0 || blockToUpdateZ >= World::ChunkWidth || blockToUpdateZ < 0)
			{
				if (!checkPositionInBounds(&blockToUpdateChunk, &blockToUpdateX, blockToUpdateY, &blockToUpdateZ))
				{
					return;
				}
				chunksToRetesselate.insert(blockToUpdateChunk);
			}

			int arrayExpansion = to1DArray(blockToUpdateX, blockToUpdateY, blockToUpdateZ);
			if (!ignoreThisSolidBlock &&
				!blockToUpdateChunk->data[arrayExpansion].isTransparent())
			{
				return;
			}

			int myOldLightLevel = blockToUpdateChunk->data[arrayExpansion].calculatedSkyLightLevel();
			blockToUpdateChunk->data[arrayExpansion].setSkyLightLevel(0);
			for (int i = 0; i < INormals3::CardinalDirections.size(); i++)
			{
				const glm::ivec3& iNormal = INormals3::CardinalDirections[i];
				const glm::ivec3 pos = glm::ivec3(blockToUpdateX + iNormal.x, blockToUpdateY + iNormal.y, blockToUpdateZ + iNormal.z);
				Block neighbor = getBlockInternal(blockToUpdateChunk, pos.x, pos.y, pos.z);
				int neighborLight = neighbor.calculatedSkyLightLevel();
				bool neighborLightEffectedByMe = (neighborLight < myOldLightLevel) || (myOldLightLevel == 31 && iNormal.y == -1);
				if (neighborLight != 0 && neighborLightEffectedByMe && neighbor.isTransparent())
				{
					Chunk* neighborChunk = blockToUpdateChunk;
					int neighborLocalX = pos.x;
					int neighborLocalZ = pos.z;
					if (checkPositionInBounds(&neighborChunk, &neighborLocalX, pos.y, &neighborLocalZ))
					{
						blocksToCheck.push(glm::ivec3(blockToUpdate.x + iNormal.x, blockToUpdate.y + iNormal.y, blockToUpdate.z + iNormal.z));
					}
					chunksToRetesselate.insert(neighborChunk);
				}
				else if (neighborLight > myOldLightLevel)
				{
					Chunk* neighborChunk = blockToUpdateChunk;
					int neighborLocalX = pos.x;
					int neighborLocalZ = pos.z;
					if (checkPositionInBounds(&neighborChunk, &neighborLocalX, pos.y, &neighborLocalZ))
					{
						lightSources.push(glm::ivec3(blockToUpdate.x + iNormal.x, blockToUpdate.y + iNormal.y, blockToUpdate.z + iNormal.z));
					}
					chunksToRetesselate.insert(neighborChunk);
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
			const glm::ivec3& lightColor,
			int skyLightLevel)
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

			// Bits  0- 1 UV Index -- this tells us which corner to use for the texture coords
			// Bit      2 Color the block based on biome
			// Bits  4- 8 Light level
			// Bits  9-17 Light color
			// Bits 17-22 Sky Light Level
			data2 |= (((uint32)uvIndex << 0) & UV_INDEX_BITMASK);
			data2 |= (((uint32)(colorVertexBasedOnBiome ? 1 : 0) << 2) & COLOR_BLOCK_BIOME_BITMASK);
			data2 |= (((uint32)(lightLevel << 3) & LIGHT_LEVEL_BITMASK));
			data2 |= (((uint32)(lightColor.r << 8) & LIGHT_COLOR_BITMASK_R));
			data2 |= (((uint32)(lightColor.g << 11) & LIGHT_COLOR_BITMASK_G));
			data2 |= (((uint32)(lightColor.b << 14) & LIGHT_COLOR_BITMASK_B));
			data2 |= (((uint32)(skyLightLevel << 17) & SKY_LIGHT_LEVEL_BITMASK));

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
			const glm::ivec3& lightColor,
			int skyLightLevel)
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

			vertexData[0] = compress(vert1, texture, face, uv0, colorFaceBasedOnBiome, lightLevel, lightColor, skyLightLevel);
			vertexData[1] = compress(vert2, texture, face, uv1, colorFaceBasedOnBiome, lightLevel, lightColor, skyLightLevel);
			vertexData[2] = compress(vert3, texture, face, uv2, colorFaceBasedOnBiome, lightLevel, lightColor, skyLightLevel);

			vertexData[3] = compress(vert1, texture, face, uv3, colorFaceBasedOnBiome, lightLevel, lightColor, skyLightLevel);
			vertexData[4] = compress(vert3, texture, face, uv4, colorFaceBasedOnBiome, lightLevel, lightColor, skyLightLevel);
			vertexData[5] = compress(vert4, texture, face, uv5, colorFaceBasedOnBiome, lightLevel, lightColor, skyLightLevel);
		}
	}
}