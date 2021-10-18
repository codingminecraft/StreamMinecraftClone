#include "world/ChunkManager.h"
#include "world/World.h"
#include "world/BlockMap.h"
#include "core/Pool.hpp"
#include "core/File.h"
#include "utils/DebugStats.h"
#include "utils/CMath.h"
#include "renderer/Shader.h"
#include "renderer/Renderer.h"

namespace Minecraft
{
	enum class CommandType : uint8
	{
		FillBlockData,
		TesselateVertices
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

	class CompareFillChunkCommand
	{
	public:
		// Returning true means lesser priority
		bool operator()(const FillChunkCommand& a, const FillChunkCommand& b)
		{
			if (a.type != b.type)
			{
				// Fill block data has higher priority than other types of commands
				return a.type != CommandType::FillBlockData;
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
			ChunkWorker(int numThreads, int worldSeed)
				: cv(), mtx(), queueMtx(), doWork(true)
			{
				seed = worldSeed;
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
						cv.wait(lock, [&] { return !doWork || !commands.empty(); });
						shouldContinue = doWork;
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
							// TODO: Fix this, this should be World::worldSavePath
							if (Chunk::exists("world", command.chunkCoordinates))
							{
								Chunk::deserialize(command.blockData, "world", command.chunkCoordinates);
							}
							else
							{
								Chunk::generate(command.blockData, command.chunkCoordinates, seed);
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

			uint32 seed;
		};

		struct DrawCommand
		{
			DrawArraysIndirectCommand command;
			int distanceToPlayer;

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
				numCommands = 0;
			}

			void init()
			{
				this->commandBuffer = (DrawCommand*)g_memory_allocate(sizeof(DrawCommand) * maxNumCommands);
				this->chunkPosBuffer = (int32*)g_memory_allocate(sizeof(int32*) * maxNumCommands * 2);
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
			}

			void add(const DrawArraysIndirectCommand& command, const glm::ivec2& chunkCoords, const glm::ivec2& playerPosChunkCoords)
			{
				g_logger_assert((numCommands + 1) < maxNumCommands, "Ran out of room in command buffer!");
				glm::ivec2 d = chunkCoords - playerPosChunkCoords;
				int dSquared = (d.x * d.x) + (d.y * d.y);
				commandBuffer[numCommands] = { command, dSquared };
				commandBuffer[numCommands].command.baseInstance = numCommands;
				chunkPosBuffer[(numCommands * 2)] = chunkCoords.x;
				chunkPosBuffer[(numCommands * 2) + 1] = chunkCoords.y;
				numCommands++;
			}

			void sort(const glm::ivec2& playerPosChunkCoords, const glm::mat4& cameraProjection, const glm::mat4& cameraView)
			{
				// Remove chunks not in the view frustum
				//for (int i = 0; i < numCommands; i++)
				//{
				//	const CommandSortOperator& chunkCoord = sortOperators[i];
				//	
				//}

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

			inline void softReset()
			{
				numCommands = 0;
			}

		private:
			int maxNumCommands;
			int numCommands;
			DrawCommand* commandBuffer;
			int32* chunkPosBuffer;
		};

		// Internal variables
		static std::mutex chunkMtx;
		static uint32 processorCount = 0;
		static uint32 worldSeed = 0;
		static std::bitset<World::ChunkCapacity> loadedChunks;
		static std::unordered_map<glm::ivec2, int> chunkIndices = {};

		//static DrawArraysIndirectCommand* gpuDrawCommandBuffer;
		static uint32 chunkPosInstancedBuffer;
		static uint32 globalVao;
		static uint32 drawCommandVbo;

		// Singletons
		static ChunkWorker& chunkWorker()
		{
			static ChunkWorker instance(processorCount, worldSeed);
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

		void init(uint32 seed)
		{
			// A chunk uses 55,000 vertices on average, so a sub-chunk can be estimated to use about 
			// 4,500 vertices on average. That's the default vertex bucket size
			processorCount = std::thread::hardware_concurrency();
			worldSeed = seed;

			// Initialize the singletons
			chunkWorker();
			blockPool();

			// Set up draw commands to relate to our sub chunks
			commandBuffer().init();
			glCreateBuffers(1, &drawCommandVbo);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, drawCommandVbo);
			glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(DrawCommand) * subChunks().size(), NULL, GL_DYNAMIC_DRAW);

			// Initialize the SubChunks
			loadedChunks.reset();

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

			// Subchunk = 16x16x16  Blocks
			// BigChunk = 16x256x16 Blocks
			g_logger_info("Vertex Pool Total Size: %2.3f Gb", (float)(totalSizeOfSubChunkVertices / (1024.0f * 1024 * 1024)));
			g_logger_info("Block Pool Total Size: %2.3f Gb", (float)(blockPool().totalSize() / (1024.0f * 1024 * 1024)));
		}

		void free()
		{
			chunkWorker().free();
			commandBuffer().free();
		}

		void queueCreateChunk(const glm::ivec2& chunkCoordinates, bool retesselate)
		{
			// Only upload if we need to
			bool upload;
			{
				std::lock_guard<std::mutex> lock(chunkMtx);
				upload = chunkIndices.find(chunkCoordinates) == chunkIndices.end();
			}
			if (upload)
			{
				FillChunkCommand cmd;
				cmd.chunkCoordinates = chunkCoordinates;
				cmd.type = CommandType::FillBlockData;
				cmd.blockData = nullptr;
				cmd.subChunks = &(subChunks());

				for (int i = 0; i < loadedChunks.size(); i++)
				{
					if (!loadedChunks.test(i))
					{
						loadedChunks.set(i, true);
						{
							std::lock_guard<std::mutex> lock(chunkMtx);
							chunkIndices[chunkCoordinates] = i;
						}
						cmd.blockData = blockPool()[i];
						chunkWorker().queueCommand(cmd);
						break;
					}
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
					auto iter = chunkIndices.find(chunkCoordinates);
					if (iter == chunkIndices.end())
					{
						return;
					}
					cmd.blockData = blockPool()[iter->second];
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

		Block getBlock(const glm::vec3& worldPosition)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			Block* blockData = nullptr;

			std::lock_guard<std::mutex> lock(chunkMtx);
			auto iter = chunkIndices.find(chunkCoords);
			if (iter != chunkIndices.end())
			{
				blockData = blockPool()[iter->second];
			}
			else if (worldPosition.y >= 0 && worldPosition.y < 256)
			{
				// Assume it's a chunk that's out of bounds
				// TODO: Make this only return air block if it's far far away from the player
				return BlockMap::AIR_BLOCK;
			}

			return Chunk::getBlock(worldPosition, chunkCoords, blockData);
		}

		void setBlock(const glm::vec3& worldPosition, Block newBlock)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			Block* blockData = nullptr;

			std::lock_guard<std::mutex> lock(chunkMtx);
			auto iter = chunkIndices.find(chunkCoords);
			if (iter != chunkIndices.end())
			{
				blockData = blockPool()[iter->second];
			}
			else if (worldPosition.y >= 0 && worldPosition.y < 256)
			{
				// Assume it's a chunk that's out of bounds
				g_logger_warning("Tried to set invalid block at position<%2.3f, %2.3f, %2.3f>!", worldPosition.x, worldPosition.y, worldPosition.z);
				return;
			}

			if (Chunk::setBlock(worldPosition, chunkCoords, blockData, newBlock))
			{
				FillChunkCommand cmd;
				cmd.chunkCoordinates = chunkCoords;
				cmd.type = CommandType::TesselateVertices;
				cmd.subChunks = &subChunks();
				cmd.blockData = blockData;

				// Update the sub-chunks that are about to be deleted
				for (int i = 0; i < subChunks().size(); i++)
				{
					if (subChunks()[i]->chunkCoordinates == chunkCoords && subChunks()[i]->state == SubChunkState::Uploaded)
					{
						subChunks()[i]->state = SubChunkState::RetesselateVertices;
					}
				}

				chunkWorker().queueCommand(cmd);
				chunkWorker().beginWork(false);
			}
		}

		void removeBlock(const glm::vec3& worldPosition)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);
			Block* blockData = nullptr;

			std::lock_guard<std::mutex> lock(chunkMtx);
			auto iter = chunkIndices.find(chunkCoords);
			if (iter != chunkIndices.end())
			{
				blockData = blockPool()[iter->second];
			}
			else if (worldPosition.y >= 0 && worldPosition.y < 256)
			{
				// Assume it's a chunk that's out of bounds
				g_logger_warning("Tried to set invalid block at position<%2.3f, %2.3f, %2.3f>!", worldPosition.x, worldPosition.y, worldPosition.z);
				return;
			}

			if (Chunk::removeBlock(worldPosition, chunkCoords, blockData))
			{
				FillChunkCommand cmd;
				cmd.chunkCoordinates = chunkCoords;
				cmd.type = CommandType::TesselateVertices;
				cmd.subChunks = &subChunks();
				cmd.blockData = blockData;

				// Update the sub-chunks that are about to be deleted
				for (int i = 0; i < subChunks().size(); i++)
				{
					if (subChunks()[i]->chunkCoordinates == chunkCoords && subChunks()[i]->state == SubChunkState::Uploaded)
					{
						subChunks()[i]->state = SubChunkState::RetesselateVertices;
					}
				}

				chunkWorker().queueCommand(cmd);
				chunkWorker().beginWork(false);
			}
		}

		void render(const glm::vec3& playerPosition, const glm::ivec2& playerPositionInChunkCoords, Shader& shader)
		{
			chunkWorker().setPlayerPosChunkCoords(playerPositionInChunkCoords);

			// TODO: Remove me, this is for debugging purposes
			static uint32 maxVertCount = 0;
			static uint32 minVertCount = UINT32_MAX;
			static uint32 last100Verts[100] = {};
			int last100VertsIndex = 0;
			float avgVertCount = 0.0f;
			for (int i = 0; i < 100; i++)
			{
				avgVertCount += last100Verts[i];
			}
			if (avgVertCount > 0)
			{
				avgVertCount /= 100;
			}
			DebugStats::minVertCount = minVertCount;
			DebugStats::maxVertCount = maxVertCount;
			DebugStats::avgVertCount = avgVertCount;

			// TODO: Weird that I have to re-enable that here. Try to find out why?
			glEnable(GL_CULL_FACE);

			for (int i = 0; i < subChunks().size(); i++)
			{
				if (subChunks()[i]->state != SubChunkState::Unloaded)
				{
					glm::ivec2 chunkPos = subChunks()[i]->chunkCoordinates;
					auto iter = chunkIndices.find(chunkPos);
					if (iter == chunkIndices.end() && subChunks()[i]->state != SubChunkState::TesselatingVertices)
					{
						// If the chunk coords are no longer loaded, set this chunk as not in use anymore
						subChunks()[i]->state = SubChunkState::Unloaded;
						subChunks()[i]->numVertsUsed = 0;
						subChunks().freePool(i);
					}
					else if (iter != chunkIndices.end() && loadedChunks.test(iter->second))
					{
						if (subChunks()[i]->state == SubChunkState::UploadVerticesToGpu)
						{
							g_logger_assert(subChunks()[i]->numVertsUsed.load() > 0, "Sub Chunk should never have tried to upload 0 verts to GPU.");
							subChunks()[i]->state = SubChunkState::Uploaded;

							// TODO: Remove me this is for debugging purposes
							last100Verts[last100VertsIndex] = subChunks()[i]->numVertsUsed;
							maxVertCount = glm::max(subChunks()[i]->numVertsUsed.load(), maxVertCount);
							minVertCount = glm::min(subChunks()[i]->numVertsUsed.load(), minVertCount);
							last100VertsIndex = (last100VertsIndex + 1) % 100;
						}

						if (subChunks()[i]->state == SubChunkState::Uploaded || subChunks()[i]->state == SubChunkState::RetesselateVertices)
						{
							DrawArraysIndirectCommand drawCommand;
							g_logger_assert(subChunks()[i]->numVertsUsed.load() > 0, "Sub Chunk should never have tried to upload 0 verts to GPU.");
							drawCommand.baseInstance = 0;
							drawCommand.instanceCount = 1;
							drawCommand.count = subChunks()[i]->numVertsUsed;
							drawCommand.first = subChunks()[i]->first;
							commandBuffer().add(drawCommand, subChunks()[i]->chunkCoordinates, playerPositionInChunkCoords);
							DebugStats::numDrawCalls++;
						}
					}
				}
			}

			if (commandBuffer().getNumCommands() > 0)
			{
				static int tick = 0;
				tick++;
				double timeStart = glfwGetTime();
				commandBuffer().sort(playerPositionInChunkCoords, glm::mat4(), glm::mat4());
				glBindBuffer(GL_ARRAY_BUFFER, chunkPosInstancedBuffer);
				glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(int32) * 2 * commandBuffer().getNumCommands(), commandBuffer().getChunkPosBuffer());
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, drawCommandVbo);
				glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(DrawCommand) * commandBuffer().getNumCommands(), commandBuffer().getCommandBuffer());

				glBindVertexArray(globalVao);
				shader.uploadVec3("uPlayerPosition", playerPosition);
				shader.uploadInt("uChunkRadius", World::ChunkRadius);
				glMultiDrawArraysIndirect(GL_TRIANGLES, NULL, commandBuffer().getNumCommands(), sizeof(DrawCommand));
				double deltaTime = glfwGetTime() - timeStart;
				if (tick >= 15)
				{
					DebugStats::chunkRenderTime = (float)deltaTime;
					tick = 0;
				}
				commandBuffer().softReset();
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
						subChunks()[i]->state = SubChunkState::Unloaded;
						subChunks()[i]->numVertsUsed = 0;

						auto iter = chunkIndices.find(chunkPos);
						if (iter != chunkIndices.end())
						{
							int chunkIndex = iter->second;
							loadedChunks.set(chunkIndex, false);
							std::lock_guard lock(chunkMtx);
							chunkIndices.erase(iter);
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
			BOTTOM_RIGHT = 3
		};

		// Internal Constants
		static const int POSITION_INDEX_BITMASK = 0x1FFFF;
		static const int TEX_ID_BITMASK = 0x1FFE0000;
		static const int FACE_BITMASK = 0xE0000000;
		static const int UV_INDEX_BITMASK = 0x3;

		static const int BASE_17_DEPTH = 17;
		static const int BASE_17_WIDTH = 17;
		static const int BASE_17_HEIGHT = 289;

		// Internal functions
		static int to1DArray(int x, int y, int z);
		static Block getBlockInternal(const Block* data, int x, int y, int z, const glm::ivec2& chunkCoordinates);
		static bool setBlockInternal(Block* data, int x, int y, int z, const glm::ivec2& chunkCoordinates, Block newBlock);
		static bool removeBlockInternal(Block* data, int x, int y, int z, const glm::ivec2& chunkCoordinates);
		static std::string getFormattedFilepath(const glm::ivec2& chunkCoordinates, const std::string& worldSavePath);
		static void loadBlock(Vertex* vertexData, const glm::ivec3& vert1, const glm::ivec3& vert2, const glm::ivec3& vert3, const glm::ivec3& vert4, const TextureFormat& texture, CUBE_FACE face);

		void info()
		{
			g_logger_info("%d size of chunk", sizeof(Block) * World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);
			g_logger_info("Max %d size of vertex data", sizeof(Vertex) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth * 24);
		}

		void generate(Block* blockData, const glm::ivec2& chunkCoordinates, int32 seed)
		{
			const int worldChunkX = chunkCoordinates.x * 16;
			const int worldChunkZ = chunkCoordinates.y * 16;

			// TODO: Should we zero the memory in release mode as well? Or does it matter?
			g_memory_zeroMem(blockData, sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth);
			const SimplexNoise generator = SimplexNoise();
			for (int y = 0; y < World::ChunkHeight; y++)
			{
				for (int x = 0; x < World::ChunkDepth; x++)
				{
					for (int z = 0; z < World::ChunkWidth; z++)
					{
						// 24 Vertices per cube
						const float incrementSize = 1000.0f;
						const int arrayExpansion = to1DArray(x, y, z);
						float maxHeightFloat =
							CMath::mapRange(
								generator.fractal(
									7,
									(x + worldChunkX + seed) / incrementSize,
									(z + worldChunkZ + seed) / incrementSize
								),
								-1.0f,
								1.0f,
								0.0f,
								1.0f
							) * 255.0f;
						int16 maxHeight = (int16)maxHeightFloat;

						float stoneHeightFloat =
							CMath::mapRange(
								generator.fractal(
									7,
									(x + worldChunkX + seed) / incrementSize,
									(z + worldChunkZ + seed) / incrementSize
								),
								-1.0f,
								1.0f,
								0.0f,
								1.0f
							) * 255.0f;
						int16 stoneHeight = (int16)((stoneHeightFloat * maxHeightFloat) / 127.0f);

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
							// Green Concrete 
							blockData[arrayExpansion].id = 5;
						}
						else
						{
							blockData[arrayExpansion].id = BlockMap::AIR_BLOCK.id;
						}
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
				ret = subChunks->getNewPool();
				ret->state = SubChunkState::TesselatingVertices;
				ret->subChunkLevel = currentLevel;
				ret->chunkCoordinates = chunkCoordinates;
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
						const int topBlockId = getBlockInternal(blockData, x, y + 1, z, chunkCoordinates).id;
						const BlockFormat& topBlock = BlockMap::getBlock(topBlockId);
						if (!topBlockId || topBlock.isTransparent)
						{
							currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
							loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[5], verts[6], verts[7], verts[4],
								top, CUBE_FACE::TOP);
							currentSubChunk->numVertsUsed += 6;
						}

						// Bottom Face
						const int bottomBlockId = getBlockInternal(blockData, x, y - 1, z, chunkCoordinates).id;
						const BlockFormat& bottomBlock = BlockMap::getBlock(bottomBlockId);
						if (!bottomBlockId || bottomBlock.isTransparent)
						{
							currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
							loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[0], verts[3], verts[2], verts[1],
								bottom, CUBE_FACE::BOTTOM);
							currentSubChunk->numVertsUsed += 6;
						}

						// Right Face
						const int rightBlockId = getBlockInternal(blockData, x, y, z + 1, chunkCoordinates).id;
						const BlockFormat& rightBlock = BlockMap::getBlock(rightBlockId);
						if (!rightBlockId || rightBlock.isTransparent)
						{
							currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
							loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[2], verts[6], verts[5], verts[1],
								side, CUBE_FACE::RIGHT);
							currentSubChunk->numVertsUsed += 6;
						}

						// Left Face
						const int leftBlockId = getBlockInternal(blockData, x, y, z - 1, chunkCoordinates).id;
						const BlockFormat& leftBlock = BlockMap::getBlock(leftBlockId);
						if (!leftBlockId || leftBlock.isTransparent)
						{
							currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
							loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[0], verts[4], verts[7], verts[3],
								side, CUBE_FACE::LEFT);
							currentSubChunk->numVertsUsed += 6;
						}

						// Forward Face
						const int forwardBlockId = getBlockInternal(blockData, x + 1, y, z, chunkCoordinates).id;
						const BlockFormat& forwardBlock = BlockMap::getBlock(forwardBlockId);
						if (!forwardBlockId || forwardBlock.isTransparent)
						{
							currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
							loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[7], verts[6], verts[2], verts[3],
								side, CUBE_FACE::FRONT);
							currentSubChunk->numVertsUsed += 6;
						}

						// Back Face
						const int backBlockId = getBlockInternal(blockData, x - 1, y, z, chunkCoordinates).id;
						const BlockFormat& backBlock = BlockMap::getBlock(backBlockId);
						if (!backBlockId || backBlock.isTransparent)
						{
							currentSubChunk = getSubChunk(subChunks, currentSubChunk, currentLevel, chunkCoordinates);
							loadBlock(currentSubChunk->data + currentSubChunk->numVertsUsed, verts[0], verts[1], verts[5], verts[4],
								side, CUBE_FACE::BACK);
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
					subChunkToUnload->numVertsUsed = 0;
					subChunkToUnload->state = SubChunkState::Unloaded;
					subChunks->freePool(i);
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

		static Vertex compress(const glm::ivec3& vertex, const TextureFormat& texture, CUBE_FACE face, UV_INDEX uvIndex)
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

			data2 |= (((uint32)uvIndex << 0) & UV_INDEX_BITMASK);

			return {
				data1,
				data2
			};
		}

		static glm::ivec3 extractPosition(uint32 data)
		{
			int positionIndex = data & POSITION_INDEX_BITMASK;
			return toCoordinates(positionIndex);
		}

		static uint16 extractTexId(uint32 data)
		{
			return (data & TEX_ID_BITMASK) >> 17;
		}

		static CUBE_FACE extractFace(uint32 data)
		{
			return (CUBE_FACE)((data & FACE_BITMASK) >> 29);
		}

		static UV_INDEX extractCorner(uint32 data)
		{
			return (UV_INDEX)((data & UV_INDEX_BITMASK) >> 0);
		}

		static void loadBlock(
			Vertex* vertexData,
			const glm::ivec3& vert1,
			const glm::ivec3& vert2,
			const glm::ivec3& vert3,
			const glm::ivec3& vert4,
			const TextureFormat& texture,
			CUBE_FACE face)
		{
			vertexData[0] = compress(vert1, texture, face, UV_INDEX::BOTTOM_RIGHT);
			vertexData[1] = compress(vert2, texture, face, UV_INDEX::TOP_RIGHT);
			vertexData[2] = compress(vert3, texture, face, UV_INDEX::TOP_LEFT);

			vertexData[3] = compress(vert1, texture, face, UV_INDEX::BOTTOM_RIGHT);
			vertexData[4] = compress(vert3, texture, face, UV_INDEX::TOP_LEFT);
			vertexData[5] = compress(vert4, texture, face, UV_INDEX::BOTTOM_LEFT);

#ifdef _DEBUG
			// TODO: Remove this once you are confident you are getting the right values
			g_logger_assert(extractPosition(vertexData[0].data1) == vert1, "Failed Position.");
			g_logger_assert(extractPosition(vertexData[1].data1) == vert2, "Failed Position.");
			g_logger_assert(extractPosition(vertexData[2].data1) == vert3, "Failed Position.");

			g_logger_assert(extractPosition(vertexData[3].data1) == vert1, "Failed Position.");
			g_logger_assert(extractPosition(vertexData[4].data1) == vert3, "Failed Position.");
			g_logger_assert(extractPosition(vertexData[5].data1) == vert4, "Failed Position.");

			g_logger_assert(extractFace(vertexData[0].data1) == face, "Failed Face");
			g_logger_assert(extractFace(vertexData[1].data1) == face, "Failed Face");
			g_logger_assert(extractFace(vertexData[2].data1) == face, "Failed Face");
			g_logger_assert(extractFace(vertexData[3].data1) == face, "Failed Face");

			g_logger_assert(extractTexId(vertexData[0].data1) == texture.id, "Failed Texture Id");
			g_logger_assert(extractTexId(vertexData[1].data1) == texture.id, "Failed Texture Id");
			g_logger_assert(extractTexId(vertexData[2].data1) == texture.id, "Failed Texture Id");
			g_logger_assert(extractTexId(vertexData[3].data1) == texture.id, "Failed Texture Id");

			g_logger_assert(extractCorner(vertexData[0].data2) == UV_INDEX::BOTTOM_RIGHT, "Failed on uv index");
			g_logger_assert(extractCorner(vertexData[1].data2) == UV_INDEX::TOP_RIGHT, "Failed on uv index");
			g_logger_assert(extractCorner(vertexData[2].data2) == UV_INDEX::TOP_LEFT, "Failed on uv index");
			g_logger_assert(extractCorner(vertexData[3].data2) == UV_INDEX::BOTTOM_RIGHT, "Failed on uv index");
			g_logger_assert(extractCorner(vertexData[4].data2) == UV_INDEX::TOP_LEFT, "Failed on uv index");
			g_logger_assert(extractCorner(vertexData[5].data2) == UV_INDEX::BOTTOM_LEFT, "Failed on uv index");
#endif
		}
	}
}