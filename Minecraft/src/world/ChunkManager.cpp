#include "world/ChunkManager.h"
#include "world/Chunk.h"
#include "world/World.h"
#include "core/Pool.hpp"
#include "utils/DebugStats.h"
#include "renderer/Shader.h"

namespace Minecraft
{
	struct ChunkThreadIndex
	{
		uint16 startIndex;
		uint16 partitionSize;
		uint16 currentChunk;
		uint16 numChunksToComplete;
	};

	struct FillChunkCommand
	{
		// Must be at least 16 subChunks per command
		SubChunk* subChunks;
		// Must be at least ChunkWidth * ChunkDepth * ChunkHeight blocks available
		Block* blockData;
		Chunk chunk;
	};

	namespace ChunkManager
	{
		class ChunkWorker
		{
		public:
			ChunkWorker(int numThreads, int worldSeed)
				: cv(), mtx(), queueMtx(), doWork(true)
			{
				//nextThreadToUse = 0;
				seed = worldSeed;
				for (int i = 0; i < numThreads; i++)
				{
					workerThreads.push_back(std::thread(&ChunkWorker::threadWorker, this));
				}

				//chunkQueue = new Chunk[World::ChunkCapacity];
				//threadIndices = new ChunkThreadIndex[numThreads];

				//uint32 partitionSize = (uint32)glm::ceil(World::ChunkCapacity / numThreads);
				//uint32 startIndex = 0;
				//for (int i = 0; i < numThreads; i++)
				//{
				//	threadIndices[i].currentChunk = 0;
				//	threadIndices[i].startIndex = startIndex;
				//	threadIndices[i].numChunksToComplete = 0;
				//	if (i != numThreads - 1)
				//	{
				//		threadIndices[i].partitionSize = partitionSize;
				//	}
				//	else
				//	{
				//		threadIndices[i].partitionSize = World::ChunkCapacity - startIndex;
				//	}

				//	workerThreads.push_back(std::thread(&ChunkWorker::threadWorker, this, i));
				//	startIndex += partitionSize;
				//}
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

				//delete[] chunkQueue;
				//delete[] threadIndices;
			}

			void threadWorker()
			{
				bool shouldContinue = true;
				while (shouldContinue)
				{
					{
						// Wait until we need to do some work
						std::unique_lock<std::mutex> lock(mtx);
						cv.wait(lock, [&] { return !doWork || !commands.empty(); });
						shouldContinue = doWork;
					}

					FillChunkCommand command;
					{
						std::lock_guard<std::mutex> queueLock(queueMtx);
						command = commands.front();
						commands.pop();
					}

					//if (chunk.loaded)
					//{
					//	g_logger_log("Deleting on CPU Chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
					//	chunk.serialize("world");
					//	chunk.freeCpu();
					//}
					g_logger_log("Creating on CPU Chunk<%d, %d>", command.chunk.chunkCoordinates.x, command.chunk.chunkCoordinates.y);
					if (Chunk::exists("world", command.chunk.chunkCoordinates.x, command.chunk.chunkCoordinates.y))
					{
						command.chunk.deserialize(command.blockData,
							"world", command.chunk.chunkCoordinates.x, command.chunk.chunkCoordinates.y);
					}
					else
					{
						command.chunk.generate(command.blockData, command.chunk.chunkCoordinates.x, command.chunk.chunkCoordinates.y, seed);
					}
					command.chunk.generateRenderData(command.subChunks);
				}
			}

			//void threadWorker(int index)
			//{
			//	bool shouldContinue = true;
			//	while (shouldContinue)
			//	{
			//		// Store a tmp variable to avoid concurrent reads
			//		uint32 tmpNumChunksToComplete = 0;
			//		{
			//			// Wait until we need to do some work
			//			std::unique_lock<std::mutex> lock(mtx);
			//			ChunkThreadIndex& threadIndex = threadIndices[index];
			//			cv.wait(lock, [&] { return !doWork || threadIndex.numChunksToComplete != 0; });
			//			tmpNumChunksToComplete = threadIndex.numChunksToComplete;
			//			shouldContinue = doWork;
			//		}

			//		std::vector<Chunk> newChunks;
			//		while (tmpNumChunksToComplete != 0)
			//		{
			//			ChunkThreadIndex& threadIndex = threadIndices[index];
			//			Chunk& chunk = chunkQueue[threadIndex.startIndex + threadIndex.currentChunk];
			//			if (chunk.loaded)
			//			{
			//				g_logger_log("Deleting on CPU Chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
			//				chunk.serialize("world");
			//				chunk.freeCpu();
			//			}
			//			else
			//			{
			//				g_logger_log("Creating on CPU Chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
			//				if (Chunk::exists("world", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y))
			//				{
			//					chunk.deserialize("world", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
			//				}
			//				else
			//				{
			//					chunk.generate(chunk.chunkCoordinates.x, chunk.chunkCoordinates.y, seed);
			//				}
			//				chunk.generateRenderData();
			//			}

			//			{
			//				std::lock_guard lock(vectorMtx);
			//				readyChunks.push_back(chunk);
			//			}

			//			{
			//				std::lock_guard lock(mtx);
			//				threadIndex.numChunksToComplete--;
			//				threadIndex.currentChunk = (threadIndex.currentChunk + 1) % threadIndex.partitionSize;
			//				// Update the tmp variable while we have this locked
			//				tmpNumChunksToComplete = threadIndex.numChunksToComplete;
			//			}
			//		}
			//	}
			//}

			void queueCommand(const FillChunkCommand& command)
			{
				{
					std::lock_guard<std::mutex> lockGuard(queueMtx);
					commands.push(command);
				}
				cv.notify_one();
			}

			//void queueChunk(const Chunk& chunk)
			//{
			//	{
			//		std::lock_guard<std::mutex> lockGuard(mtx);
			//		g_logger_assert(nextThreadToUse >= 0 && nextThreadToUse < workerThreads.size(), "Invalid thread worker %d.", nextThreadToUse);
			//		ChunkThreadIndex& index = threadIndices[nextThreadToUse];
			//		g_logger_assert(index.numChunksToComplete < index.partitionSize - 1, "Ran out of room.");
			//		uint32 nextAvailableSlot =
			//			index.startIndex + ((index.currentChunk + index.numChunksToComplete) % index.partitionSize);
			//		chunkQueue[nextAvailableSlot] = chunk;
			//		index.numChunksToComplete++;
			//	}
			//	nextThreadToUse = (nextThreadToUse + 1) % workerThreads.size();
			//	cv.notify_one();
			//}

			//std::vector<Chunk> getReadyChunks()
			//{
			//	if (vectorMtx.try_lock())
			//	{
			//		if (readyChunks.size() > 0)
			//		{
			//			std::vector<Chunk> copy = readyChunks;
			//			readyChunks.clear();
			//			vectorMtx.unlock();
			//			return copy;
			//		}
			//		else
			//		{
			//			vectorMtx.unlock();
			//		}
			//	}

			//	return {};
			//}

		private:
			//ChunkThreadIndex* threadIndices;
			//uint32 nextThreadToUse;
			//std::vector<Chunk> readyChunks;
			std::queue<FillChunkCommand> commands;
			std::vector<std::thread> workerThreads;
			std::condition_variable cv;
			std::mutex mtx;
			std::mutex queueMtx;
			bool doWork;

			uint32 seed;
		};

		static uint32 processorCount = 0;
		static uint32 worldSeed = 0;
		static std::array<SubChunk, World::ChunkCapacity * 16> subChunks = {};
		static std::array<Chunk, World::ChunkCapacity> chunks = {};
		static std::bitset<World::ChunkCapacity> loadedChunks;
		static std::unordered_map<glm::ivec2, int> chunkIndices = {};

		static ChunkWorker& chunkWorker()
		{
			static ChunkWorker instance(processorCount, worldSeed);
			return instance;
		}

		static Pool<Vertex, World::ChunkCapacity * 16>& vertexPool()
		{
			// 16 SubChunks per chunk
			static Pool<Vertex, World::ChunkCapacity * 16> instance(World::MaxVertsPerSubChunk);
			return instance;
		}

		static Pool<Block, World::ChunkCapacity>& blockPool()
		{
			static Pool<Block, World::ChunkCapacity> instance(World::ChunkDepth * World::ChunkWidth * World::ChunkHeight);
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
			Pool<Vertex, World::ChunkCapacity * 16>& vertexPools = vertexPool();

			// Initialize the SubChunks
			loadedChunks.reset();
			for (int i = 0; i < vertexPools.size(); i++)
			{
				// Assign the pointers for the data on the CPU
				subChunks.at(i).data = vertexPools[i];
				subChunks.at(i).numVertsUsed = 0;
				subChunks.at(i).loaded = false;

				// Generate a bunch of empty vertex buckets for GPU use
				glCreateVertexArrays(1, &subChunks.at(i).vao);
				glBindVertexArray(subChunks.at(i).vao);

				glGenBuffers(1, &subChunks.at(i).vbo);

				glBindBuffer(GL_ARRAY_BUFFER, subChunks.at(i).vbo);
				glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * vertexPools.poolSize(), NULL, GL_DYNAMIC_DRAW);

				// Set our vertex attribute pointers
				glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(Vertex), (void*)offsetof(Vertex, data1));
				glEnableVertexAttribArray(0);

				glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(Vertex), (void*)(offsetof(Vertex, data2)));
				glEnableVertexAttribArray(1);
			}

			// Subchunk = 16x16x16  Blocks
			// BigChunk = 16x256x16 Blocks
			g_logger_info("Vertex Pool Total Size: %2.3f Gb", (float)(vertexPool().totalSize() / (1024.0f * 1024 * 1024)));
			g_logger_info("Block Pool Total Size: %2.3f Gb", (float)(blockPool().totalSize() / (1024.0f * 1024 * 1024)));
		}

		void free()
		{
			chunkWorker().free();
		}

		void queueCreateChunk(int32 x, int32 z)
		{
			// Only upload if we need to
			if (chunkIndices.find({ x, z }) == chunkIndices.end())
			{
				FillChunkCommand cmd;
				cmd.chunk.chunkCoordinates.x = x;
				cmd.chunk.chunkCoordinates.y = z;
				//chunk.loaded = false;
				//chunkWorker().queueChunk(chunk);
				cmd.blockData = nullptr;
				cmd.subChunks = nullptr;

				for (int i = 0; i < loadedChunks.size(); i++)
				{
					if (!loadedChunks.test(i))
					{
						cmd.subChunks = subChunks.data() + (i * 16);
						loadedChunks.set(i, true);
						chunkIndices[{x, z}] = i;
						cmd.blockData = blockPool()[i];
						break;
					}
				}

				g_logger_assert(cmd.subChunks != nullptr, "Ran out of chunks! Cannot find any new chunk data.");
				g_logger_info("Queueing chunk<%d, %d>", cmd.chunk.chunkCoordinates.x, cmd.chunk.chunkCoordinates.y);
				chunkWorker().queueCommand(cmd);
			}
		}

		Chunk getChunk(const glm::vec3& worldPosition)
		{
			glm::ivec2 chunkCoords = World::toChunkCoords(worldPosition);

			Chunk res;
			res.chunkCoordinates = chunkCoords;
			res.chunkData = nullptr;

			auto iter = chunkIndices.find(chunkCoords);
			if (iter != chunkIndices.end())
			{
				res.chunkData = blockPool()[iter->second];
			}

			return res;
		}

		void render(const glm::vec3& playerPosition, const glm::ivec2& playerPositionInChunkCoords, Shader& shader)
		{
			for (auto iter = chunkIndices.begin(); iter != chunkIndices.end();)
			{
				glm::ivec2 chunkPos = iter->first;
				int chunkIndex = iter->second;

				if (loadedChunks.test(chunkIndex))
				{
					shader.uploadIVec2("uChunkPos", chunkPos);
					shader.uploadVec3("uPlayerPosition", playerPosition);
					shader.uploadInt("uChunkRadius", World::ChunkRadius);

					SubChunk* data = subChunks.data() + (chunkIndex * 16);
					for (int i = 0; i < 16; i++)
					{
						if (data[i].uploadVertsToGpu)
						{
							data[i].uploadVertsToGpu = false;
							glBindBuffer(GL_ARRAY_BUFFER, data[i].vbo);
							glBufferSubData(GL_ARRAY_BUFFER, 0, data[i].numVertsUsed * sizeof(Vertex), data[i].data);
							g_logger_info("Num Verts Used: %d", data[i].numVertsUsed.load());
						}

						if (data[i].numVertsUsed > 0)
						{
							glBindVertexArray(data[i].vao);
							glDrawArrays(GL_TRIANGLES, 0, data[i].numVertsUsed);
							DebugStats::numDrawCalls++;
						}
					}

					const glm::ivec2 localChunkPos = chunkPos - playerPositionInChunkCoords;
					if ((localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) >=
						(World::ChunkRadius * World::ChunkRadius) * (World::ChunkRadius * World::ChunkRadius))
					{
						g_logger_info("Deleting chunk<%d, %d> %d, %d", chunkPos.x, chunkPos.y, localChunkPos.x, localChunkPos.y);
						int chunkIndex = iter->second;
						loadedChunks.set(chunkIndex, false);
						iter = chunkIndices.erase(iter);
					}
					else
					{
						iter++;
					}
				}
				else
				{
					iter++;
				}
			}
		}
	}
}