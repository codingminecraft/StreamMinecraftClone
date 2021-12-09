#include "world/ChunkThreadWorker.h"
#include "world/Chunk.hpp"
#include "world/World.h"
#include "world/BlockMap.h"
#include "network/Network.h"
#include "utils/DebugStats.h"

namespace Minecraft
{
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
		noiseGenerators[0] = SimplexNoise();// World::seedAsFloat.load());
		noiseGenerators[1] = SimplexNoise(World::seedAsFloat.load());
		noiseGenerators[2] = SimplexNoise(World::seedAsFloat.load());
		noiseGenerators[3] = SimplexNoise(World::seedAsFloat.load());
		noiseGenerators[4] = SimplexNoise(World::seedAsFloat.load());

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
					for (int i = 0; i < World::ChunkWidth * World::ChunkDepth * World::ChunkHeight; i++)
					{
						BlockFormat format = BlockMap::getBlock(command.chunk->data[i].id);
						command.chunk->data[i].setTransparent(format.isTransparent);
						command.chunk->data[i].setIsLightSource(format.isLightSource);
						command.chunk->data[i].setIsBlendable(format.isBlendable);
					}
					command.chunk->needsToGenerateDecorations = false;
					command.chunk->needsToCalculateLighting = true;
					break;
				}
				case CommandType::GenerateTerrain:
				{
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
						ChunkPrivate::generateTerrain(command.chunk, command.chunk->chunkCoords, World::seedAsFloat, noiseGenerators[0]);
						command.chunk->needsToGenerateDecorations = true;
					}
					command.chunk->needsToCalculateLighting = true;
				}
				break;
				case CommandType::GenerateDecorations:
				{
					if (Network::isNetworkEnabled())
					{
						return;
					}

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
					ChunkPrivate::serialize(World::chunkSavePath, *command.chunk);

					// Tell the chunk manager we are done
					command.chunk->state = ChunkState::Unloading;
				}
				break;
				}
			}
		}
	}

	void ChunkThreadWorker::queueCommand(FillChunkCommand& command)
	{
		command.playerPosChunkCoords = playerPosChunkCoords.load();
		{
			std::lock_guard<std::mutex> lockGuard(queueMtx);
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

	void ChunkThreadWorker::setPlayerPosChunkCoords(const glm::ivec2& playerPosChunkCoords)
	{
		this->playerPosChunkCoords = playerPosChunkCoords;
	}
}
