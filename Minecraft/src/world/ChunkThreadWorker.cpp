#include "world/ChunkThreadWorker.h"
#include "world/Chunk.hpp"
#include "world/World.h"
#include "world/BlockMap.h"
#include "core/Application.h"
#include "core/GlobalThreadPool.h"
#include "network/Network.h"
#include "utils/DebugStats.h"

namespace Minecraft
{
	// Internal functions
	static void freeChunkCmd(void* fillChunkCmd, size_t dataSize);
	static void clientLoadChunk(void* fillChunkCmd, size_t dataSize);
	static void generateTerrain(void* fillChunkCmd, size_t dataSize);
	static void generateDecorations(FillChunkCommand* fillChunkCmd);
	static void calculateLighting(FillChunkCommand* fillChunkCmd);
	static void recalculateLighting(void* fillChunkCmd, size_t dataSize);
	static void tesselateVertices(void* fillChunkCmd, size_t dataSize);
	static void saveBlockData(void* fillChunkCmd, size_t dataSize);
	static bool isSynchronous(FillChunkCommand* command);

	// Internal members
	static uint32 barrierSyncCounter = 0;
	static uint32 barrierSyncPoint = 0;
	// Used for tracking progress
	static uint32 totalCommandCount = 0;
	static uint32 totalCommandsDone = 0;
	static std::mutex barrierMtx;

	bool CompareFillChunkCommand::operator()(const FillChunkCommand& a, const FillChunkCommand& b) const
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

	ChunkThreadWorker::ChunkThreadWorker()
		: cv(), mtx(), queueMtx(), doWork(true)
	{
		workerThread = std::thread(&ChunkThreadWorker::threadWorker, this);
	}

	void ChunkThreadWorker::free()
	{
		{
			std::lock_guard lock(mtx);
			doWork = false;
		}
		cv.notify_all();

		workerThread.join();
	}

	void ChunkThreadWorker::threadWorker()
	{
#ifdef _USE_OPTICK
		OPTICK_THREAD("ChunkThreadWorker");
#endif

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

			FillChunkCommand* command = (FillChunkCommand*)g_memory_allocate(sizeof(FillChunkCommand));
			bool processCommand = false;
			{
				std::lock_guard<std::mutex> queueLock(queueMtx);
				if (!commands.empty())
				{
					// Only process all commands if we're not stopping the thread worker.
					// If we are stopping the thread worker, then only process save commands
					do
					{
						g_memory_copyMem(command, (void*)&commands.top(), sizeof(FillChunkCommand));
						commands.pop();
						processCommand = (!doWork && command->type == CommandType::SaveBlockData) || doWork;
					} while (!doWork && command->type != CommandType::SaveBlockData && commands.size() > 0);
				}
			}

			if (isSynchronous(command))
			{
				std::unique_lock<std::mutex> lock(mtx);
				cv2.wait(lock, [&] { return !doWork || barrierSyncCounter >= barrierSyncPoint; });

				std::lock_guard<std::mutex> barrierLock(barrierMtx);
				barrierSyncCounter = 0;
				barrierSyncPoint = 0;
			}
			else
			{
				std::lock_guard<std::mutex> lock(barrierMtx);
				barrierSyncPoint++;
			}

			if (processCommand)
			{
				switch (command->type)
				{
				case CommandType::SaveBlockData:
				{
					// SYNCHRONOUS for now, once we switch to unlimited height, this can become asynchronous
					//Application::getGlobalThreadPool().queueTask(saveBlockData, command, sizeof(FillChunkCommand), Priority::High, freeChunkCmd);
					saveBlockData(command, sizeof(FillChunkCommand));
					freeChunkCmd(command, sizeof(FillChunkCommand));
					break;
				}
				case CommandType::ClientLoadChunk:
					Application::getGlobalThreadPool().queueTask(clientLoadChunk, "ClientLoadChunk", command, sizeof(FillChunkCommand), Priority::High, freeChunkCmd);
					break;
				case CommandType::GenerateTerrain:
				{
#ifdef _USE_OPTICK
					OPTICK_EVENT("GenerateTerrain");
#endif
					Application::getGlobalThreadPool().queueTask(generateTerrain, "GenerateTerrain", command, sizeof(FillChunkCommand), Priority::High, freeChunkCmd);
					break;
				}
				case CommandType::GenerateDecorations:
				{
#ifdef _USE_OPTICK
					OPTICK_EVENT("GenerateDecorations");
#endif
					// SYNCHRONOUS
					generateDecorations(command);
					freeChunkCmd(command, sizeof(FillChunkCommand));
					break;
				}
				case CommandType::CalculateLighting:
				{
#ifdef _USE_OPTICK
					OPTICK_EVENT("CalculateLighting");
#endif
					// SYNCHRONOUS
					calculateLighting(command);
					freeChunkCmd(command, sizeof(FillChunkCommand));
					break;
				}
				case CommandType::RecalculateLighting:
				{
#ifdef _USE_OPTICK
					OPTICK_EVENT("RecalculateLighting");
#endif
					Application::getGlobalThreadPool().queueTask(recalculateLighting, "RecalculateLighting", command, sizeof(FillChunkCommand), Priority::High, freeChunkCmd);
					break;
				}
				case CommandType::TesselateVertices:
				{
#ifdef _USE_OPTICK
					OPTICK_EVENT("TesselateVertices");
#endif
					Application::getGlobalThreadPool().queueTask(tesselateVertices, "TesselateVertices", command, sizeof(FillChunkCommand), Priority::High, freeChunkCmd);
					break;
				}
				}

				Application::getGlobalThreadPool().beginWork(false);
			}
			else
			{
				g_memory_free(command);
			}
		}
	}

	void ChunkThreadWorker::queueCommand(FillChunkCommand& command)
	{
		command.playerPosChunkCoords = playerPosChunkCoords.load();
		{
			std::lock_guard<std::mutex> lockGuard(queueMtx);
			std::lock_guard<std::mutex> lockGuard2(barrierMtx);
			totalCommandCount++;
			commands.push(command);
		}
	}

	void ChunkThreadWorker::beginWork(bool notifyAll)
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

	void ChunkThreadWorker::wakeupCv2()
	{
		cv2.notify_all();
	}

	void ChunkThreadWorker::setPlayerPosChunkCoords(const glm::ivec2& playerPosChunkCoords)
	{
		this->playerPosChunkCoords = playerPosChunkCoords;
	}

	float ChunkThreadWorker::percentDone()
	{
		std::lock_guard<std::mutex> lock(barrierMtx);
		if (initialSize == -1.0f)
		{
			initialSize = (float)totalCommandCount;
		}
		return (float)totalCommandsDone >= initialSize ? 1.0f : 1.0f - ((initialSize - (float)totalCommandsDone) / initialSize);
	}

	static bool isSynchronous(FillChunkCommand* command)
	{
		switch (command->type)
		{
		case CommandType::CalculateLighting:
		case CommandType::GenerateDecorations:
			return true;
		}

		return false;
	}

	static void clientLoadChunk(void* fillChunkCmd, size_t dataSize)
	{
		g_logger_assert(dataSize == sizeof(FillChunkCommand), "Invalid data size sent to task 'clientLoadChunk'.\nExpected '%zu', but got '%zu'", sizeof(FillChunkCommand), dataSize);
		FillChunkCommand& command = *(FillChunkCommand*)fillChunkCmd;

		g_logger_assert(command.clientChunkData != nullptr, "Invalid client data sent to the chunk.");
		g_memory_copyMem(command.chunk->data, command.clientChunkData,
			sizeof(Block) * World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);
		g_memory_free(command.clientChunkData);
		for (int i = 0; i < World::ChunkWidth * World::ChunkDepth * World::ChunkHeight; i++)
		{
			BlockFormat format = BlockMap::getBlock(command.chunk->data[i].id);
			command.chunk->data[i].setTransparent(format.isTransparent);
			command.chunk->data[i].setIsLightSource(format.isLightSource);
			command.chunk->data[i].setIsBlendable(format.isBlendable);
		}
		command.chunk->needsToGenerateDecorations = false;
		command.chunk->needsToCalculateLighting = true;
	}

	static void generateTerrain(void* fillChunkCmd, size_t dataSize)
	{
		g_logger_assert(dataSize == sizeof(FillChunkCommand), "Invalid data size sent to task 'generateTerrain'.\nExpected '%zu', but got '%zu'", sizeof(FillChunkCommand), dataSize);
		FillChunkCommand& command = *(FillChunkCommand*)fillChunkCmd;

		if (Network::isNetworkEnabled())
		{
			return;
		}

		if (ChunkPrivate::exists(World::chunkSavePath, command.chunk->chunkCoords))
		{
			ChunkPrivate::deserialize(*command.chunk, World::chunkSavePath);
			command.chunk->needsToGenerateDecorations = false;
		}
		else
		{
			ChunkPrivate::generateTerrain(command.chunk, command.chunk->chunkCoords, World::seedAsFloat);
			command.chunk->needsToGenerateDecorations = true;
		}
		command.chunk->needsToCalculateLighting = true;
	}

	static void generateDecorations(FillChunkCommand* fillChunkCmd)
	{
		if (Network::isNetworkEnabled())
		{
			return;
		}

		ChunkPrivate::generateDecorations(fillChunkCmd->playerPosChunkCoords, World::seedAsFloat);
	}

	static void calculateLighting(FillChunkCommand* fillChunkCmd)
	{
		ChunkPrivate::calculateLighting(fillChunkCmd->playerPosChunkCoords);
	}

	static void recalculateLighting(void* fillChunkCmd, size_t dataSize)
	{
		g_logger_assert(dataSize == sizeof(FillChunkCommand), "Invalid data size sent to task 'clientLoadChunk'.\nExpected '%zu', but got '%zu'", sizeof(FillChunkCommand), dataSize);
		FillChunkCommand& command = *(FillChunkCommand*)fillChunkCmd;

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

	static void tesselateVertices(void* fillChunkCmd, size_t dataSize)
	{
		g_logger_assert(dataSize == sizeof(FillChunkCommand), "Invalid data size sent to task 'clientLoadChunk'.\nExpected '%zu', but got '%zu'", sizeof(FillChunkCommand), dataSize);
		FillChunkCommand& command = *(FillChunkCommand*)fillChunkCmd;

		ChunkPrivate::generateRenderData(command.subChunks, command.chunk, command.chunk->chunkCoords, command.isRetesselating);
	}

	static void saveBlockData(void* fillChunkCmd, size_t dataSize)
	{
		g_logger_assert(dataSize == sizeof(FillChunkCommand), "Invalid data size sent to task 'clientLoadChunk'.\nExpected '%zu', but got '%zu'", sizeof(FillChunkCommand), dataSize);
		FillChunkCommand& command = *(FillChunkCommand*)fillChunkCmd;

		// Unload all sub-chunks
		for (int i = 0; i < (int)command.subChunks->size(); i++)
		{
			if ((*command.subChunks)[i]->state != SubChunkState::Unloaded && (*command.subChunks)[i]->chunkCoordinates == command.chunk->chunkCoords)
			{
				(*command.subChunks)[i]->state = SubChunkState::Unloaded;
				(*command.subChunks)[i]->numVertsUsed = 0;
				command.subChunks->freePool(i);
				DebugStats::totalChunkRamUsed = DebugStats::totalChunkRamUsed - (World::MaxVertsPerSubChunk * sizeof(Vertex));
			}
		}

		// Serialize block data
		ChunkPrivate::serialize(World::chunkSavePath, *command.chunk);

		// Tell the chunk manager we are done
		command.chunk->state = ChunkState::Unloading;
	}

	static void freeChunkCmd(void* fillChunkCmd, size_t dataSize)
	{
		bool startWork = false;
		{
			std::lock_guard<std::mutex> lock(barrierMtx);
			barrierSyncCounter++;
			totalCommandsDone++;
			if (barrierSyncCounter >= barrierSyncPoint)
			{
				startWork = true;
			}
		}

		if (startWork)
		{
			// Wake up the conditional variable if needed
			ChunkManager::wakeUpCv2();
		}
		g_memory_free(fillChunkCmd);
	}
}
