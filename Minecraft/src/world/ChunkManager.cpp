#include "world/ChunkManager.h"
#include "world/Chunk.h"
#include "world/World.h"

namespace Minecraft
{
	struct ChunkThreadIndex
	{
		uint16 startIndex;
		uint16 partitionSize;
		uint16 currentChunk;
		uint16 numChunksToComplete;
	};

	namespace ChunkManager
	{
		class ChunkWorker
		{
		public:
			ChunkWorker(int numThreads, int worldSeed)
				: cv(), mtx(), vectorMtx(), doWork(true)
			{
				nextThreadToUse = 0;
				seed = worldSeed;
				chunkQueue = new Chunk[World::ChunkCapacity];
				threadIndices = new ChunkThreadIndex[numThreads];

				uint32 partitionSize = (uint32)glm::ceil(World::ChunkCapacity / numThreads);
				uint32 startIndex = 0;
				for (int i = 0; i < numThreads; i++)
				{
					threadIndices[i].currentChunk = 0;
					threadIndices[i].startIndex = startIndex;
					threadIndices[i].numChunksToComplete = 0;
					if (i != numThreads - 1)
					{
						threadIndices[i].partitionSize = partitionSize;
					}
					else
					{
						threadIndices[i].partitionSize = World::ChunkCapacity - startIndex;
					}

					workerThreads.push_back(std::thread(&ChunkWorker::threadWorker, this, i));
					startIndex += partitionSize;
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

				delete[] chunkQueue;
				delete[] threadIndices;
			}

			void threadWorker(int index)
			{
				bool shouldContinue = true;
				while (shouldContinue)
				{
					// Store a tmp variable to avoid concurrent reads
					uint32 tmpNumChunksToComplete = 0;
					{
						// Wait until we need to do some work
						std::unique_lock<std::mutex> lock(mtx);
						ChunkThreadIndex& threadIndex = threadIndices[index];
						cv.wait(lock, [&] { return !doWork || threadIndex.numChunksToComplete != 0; });
						tmpNumChunksToComplete = threadIndex.numChunksToComplete;
						shouldContinue = doWork;
					}

					std::vector<Chunk> newChunks;
					while (tmpNumChunksToComplete != 0)
					{
						ChunkThreadIndex& threadIndex = threadIndices[index];
						Chunk& chunk = chunkQueue[threadIndex.startIndex + threadIndex.currentChunk];
						if (chunk.loaded)
						{
							g_logger_info("Deleting on CPU Chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
							chunk.serialize("world");
							chunk.freeCpu();
						}
						else
						{
							g_logger_info("Creating on CPU Chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
							if (Chunk::exists("world", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y))
							{
								chunk.deserialize("world", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
							}
							else
							{
								chunk.generate(chunk.chunkCoordinates.x, chunk.chunkCoordinates.y, seed);
							}
							chunk.generateRenderData();
						}

						{
							std::lock_guard lock(vectorMtx);
							readyChunks.push_back(chunk);
						}

						{
							std::lock_guard lock(mtx);
							threadIndex.numChunksToComplete--;
							threadIndex.currentChunk = (threadIndex.currentChunk + 1) % threadIndex.partitionSize;
							// Update the tmp variable while we have this locked
							tmpNumChunksToComplete = threadIndex.numChunksToComplete;
						}
					}
				}
			}

			void queueChunk(const Chunk& chunk)
			{
				{
					std::lock_guard<std::mutex> lockGuard(mtx);
					g_logger_assert(nextThreadToUse >= 0 && nextThreadToUse < workerThreads.size(), "Invalid thread worker %d.", nextThreadToUse);
					ChunkThreadIndex& index = threadIndices[nextThreadToUse];
					g_logger_assert(index.numChunksToComplete < index.partitionSize - 1, "Ran out of room.");
					uint32 nextAvailableSlot =
						index.startIndex + ((index.currentChunk + index.numChunksToComplete) % index.partitionSize);
					chunkQueue[nextAvailableSlot] = chunk;
					index.numChunksToComplete++;
				}
				nextThreadToUse = (nextThreadToUse + 1) % workerThreads.size();
				cv.notify_one();
			}

			std::vector<Chunk> getReadyChunks()
			{
				if (vectorMtx.try_lock())
				{
					if (readyChunks.size() > 0)
					{
						std::vector<Chunk> copy = readyChunks;
						readyChunks.clear();
						vectorMtx.unlock();
						return copy;
					}
					else
					{
						vectorMtx.unlock();
					}
				}

				return {};
			}

		private:
			Chunk* chunkQueue;
			ChunkThreadIndex* threadIndices;
			uint32 nextThreadToUse;
			std::vector<Chunk> readyChunks;
			std::vector<std::thread> workerThreads;
			std::condition_variable cv;
			std::mutex mtx;
			std::mutex vectorMtx;
			bool doWork;

			uint32 seed;
		};

		static uint32 processorCount = 0;
		static uint32 worldSeed = 0;

		static ChunkWorker& chunkWorker()
		{
			static ChunkWorker worker(processorCount, worldSeed);
			return worker;
		}

		void init(uint32 seed)
		{
			processorCount = std::thread::hardware_concurrency();
			worldSeed = seed;

			// Initialize the singleton
			chunkWorker();
		}

		void free()
		{
			chunkWorker().free();
		}

		void queueCreateChunk(int32 x, int32 z)
		{
			Chunk chunk{};
			chunk.chunkCoordinates.x = x;
			chunk.chunkCoordinates.y = z;
			chunk.loaded = false;
			chunkWorker().queueChunk(chunk);
		}

		void queueDeleteChunk(const Chunk& chunk)
		{
			chunkWorker().queueChunk(chunk);
		}

		std::vector<Chunk> getReadyChunks()
		{
			return chunkWorker().getReadyChunks();
		}
	}
}