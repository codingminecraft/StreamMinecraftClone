#include "network/Client.h"
#include "network/TransformCommandBuffer.h"
#include "network/Network.h"
#include "network/Server.h"
#include "core.h"
#include "core/Scene.h"
#include "core/Components.h"
#include "world/ChunkManager.h"
#include "world/Chunk.hpp"
#include "world/BlockMap.h"
#include "gameplay/PlayerController.h"
#include "gui/MainHud.h"

#include <enet/enet.h>

#undef min
namespace Minecraft
{
	namespace Client
	{
		// Internal variables
		static ENetAddress address;
		static ENetHost* client;
		static ENetPeer* peer;

		static TransformCommandBuffer transformCommandBuffer;
		static constexpr uint64 lagInMs = 300;
		static constexpr int maxNumTransformCommands = 3000;
		static bool isConnectingVar;

		// The time that this client is drifting from the server in milliseconds
		static uint64 serverTimeDrift;
		uint64 clientGameTime = 0;

		// Internal functions
		static void processEvent(NetworkEvent* event, uint8* data);
		static void processUserCommand(UserCommand* command, void* userCommandData);
		static void processClientCommand(ClientCommand* command, void* userCommandData);

		void init()
		{
			serverTimeDrift = 0;
			clientGameTime = 0;
			isConnectingVar = false;

			client = enet_host_create(
				NULL, // create a client host
				1, // only allow 1 outgoing connection
				2, // allow up 2 channels to be used, 0 and 1
				0, // assume any amount of incoming bandwidth
				0); // assume any amount of outgoing bandwidth

			if (client == NULL)
			{
				g_logger_assert(false, "An error occurred while trying to create an ENet client host.");
				return;
			}

			// Create a peer that is connected to the server
			peer = enet_host_connect(client, &address, 2, 0);
			if (peer == NULL)
			{
				g_logger_assert(false, "No available peers for initiating an ENet connection.");
				return;
			}

			// Check if the connection succeeded
			ENetEvent event;
			if (enet_host_service(client, &event, 15000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
			{
				isConnectingVar = true;
				g_logger_info("Successfully connected to '%d':'%d'", address.host, address.port);
			}
			else
			{
				enet_peer_reset(peer);
				g_logger_error("Failed to connect to '%d':'%d'", address.host, address.port);
				peer = NULL;
				free();
				return;
			}

			transformCommandBuffer.init(maxNumTransformCommands);
		}

		void update()
		{
			clientGameTime += (uint64)(World::deltaTime * 1000.0f);
			ENetEvent event;

			// Process all events
			while (enet_host_service(client, &event, 0) > 0)
			{
				switch (event.type)
				{
				case ENET_EVENT_TYPE_CONNECT:
				{
					g_logger_info("A new client connected from %x:%u.", event.peer->address.host, event.peer->address.port);
					g_logger_assert(false, "Is this ever even hit? If not remove this.");
					break;
				}
				case ENET_EVENT_TYPE_RECEIVE:
				{
					NetworkEventData networkEventData = Network::deserializeNetworkEvent(event.packet->data, event.packet->dataLength);
					processEvent(networkEventData.event, networkEventData.data);

					// Clean up the packet now that we're done using it
					enet_packet_destroy(event.packet);
					break;
				}
				case ENET_EVENT_TYPE_DISCONNECT:
				{
					g_logger_info("%s disconnected.", event.peer->data);

					// Reset the peer's client information
					event.peer->data = NULL;
					break;
				}
				}
			}
		}

		bool isConnecting()
		{
			return isConnectingVar;
		}

		void sendServer(ENetPacket* packet)
		{
			enet_peer_send(peer, 0, packet);
		}

		void free()
		{
			enet_peer_disconnect(peer, 0);
			peer = NULL;

			// Process all the events
			update();
			transformCommandBuffer.free();

			enet_host_destroy(client);
			client = NULL;

			address.host = 0;
			address.port = 0;
		}

		void setAddress(const ENetAddress& inAddress)
		{
			address.host = inAddress.host;
			address.port = inAddress.port;
		}

		static void processEvent(NetworkEvent* event, uint8* data)
		{
			switch (event->type)
			{
			case NetworkEventType::ChunkData:
			{
				g_logger_info("Recieving server chunk data.");
				uint16 numChunks;
				uint8* chunkDataPtr = data;
				g_memory_copyMem(&numChunks, chunkDataPtr, sizeof(uint16));
				chunkDataPtr += sizeof(uint16);
				g_logger_info("Num chunks: %u", numChunks);

				for (uint16 i = 0; i < numChunks; i++)
				{
					uint32 compressedChunkSize;
					g_memory_copyMem(&compressedChunkSize, chunkDataPtr, sizeof(uint32));
					chunkDataPtr += sizeof(uint32);

					Block* chunkData = (Block*)g_memory_allocate(sizeof(Block) * World::ChunkWidth * World::ChunkHeight * World::ChunkDepth);
					g_memory_zeroMem(chunkData, sizeof(Block) * World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);
					int blockIndex = 0;
					uint32 chunkByteCounter = 0;
					while (chunkByteCounter < compressedChunkSize)
					{
						uint16 blockId;
						uint16 blockCount;
						g_memory_copyMem(&blockId, chunkDataPtr, sizeof(uint16));
						chunkDataPtr += sizeof(uint16);
						chunkByteCounter += sizeof(uint16);
						g_memory_copyMem(&blockCount, chunkDataPtr, sizeof(uint16));
						chunkDataPtr += sizeof(uint16);
						chunkByteCounter += sizeof(uint16);

						g_logger_assert(blockIndex + blockCount <= World::ChunkWidth * World::ChunkDepth * World::ChunkHeight,
							"Encountered bad data while serializing chunk data.");
						int maxBlockCount = glm::min((int)blockCount, World::ChunkWidth * World::ChunkDepth * World::ChunkHeight);
						const BlockFormat& blockFormat = BlockMap::getBlock(blockId);
						for (int blockCounter = 0; blockCounter < maxBlockCount; blockCounter++)
						{
							chunkData[blockIndex].id = blockId;
							chunkData[blockIndex].setLightLevel(0);
							chunkData[blockIndex].setSkyLightLevel(0);
							chunkData[blockIndex].setLightColor(glm::ivec3(255, 255, 255));
							chunkData[blockIndex].setTransparent(blockFormat.isTransparent);
							chunkData[blockIndex].setIsBlendable(blockFormat.isBlendable);
							chunkData[blockIndex].setIsLightSource(blockFormat.isLightSource);
							blockIndex++;
						}
					}
					g_logger_assert(blockIndex == World::ChunkWidth * World::ChunkDepth * World::ChunkHeight,
						"Deserialized invalid block data on client. Count was '%d', should be '%d'", blockIndex, World::ChunkWidth * World::ChunkHeight * World::ChunkDepth);
					int32 chunkX, chunkZ;
					ChunkState state;
					g_memory_copyMem(&chunkX, chunkDataPtr, sizeof(int32));
					chunkDataPtr += sizeof(int32);
					g_memory_copyMem(&chunkZ, chunkDataPtr, sizeof(int32));
					chunkDataPtr += sizeof(int32);
					g_memory_copyMem(&state, chunkDataPtr, sizeof(ChunkState));
					chunkDataPtr += sizeof(ChunkState);

					ChunkManager::queueClientLoadChunk(chunkData, glm::ivec2(chunkX, chunkZ), state);
				}
				g_logger_assert(chunkDataPtr - data == event->dataSize, "Deserialized more or less data than we recieved from the client.");
			}
			break;
			case NetworkEventType::PatchChunkNeighbors:
			{
				g_logger_info("Patching chunk pointers.");
				ChunkManager::patchChunkPointers();
				g_logger_info("Done patching chunk pointers.");
			}
			break;
			case NetworkEventType::NotifyChunkWorker:
			{
				g_logger_info("Starting the work on the thread.");
				ChunkManager::beginWork();
			}
			break;
			case NetworkEventType::WorldSeed:
			{
				uint32 worldSeed = *(uint32*)(char*)(data);
				World::seed = worldSeed;
				World::seedAsFloat = (float)((double)worldSeed / (double)UINT32_MAX) * 2.0f - 1.0f;
				g_logger_info("Client received world seed: %u", worldSeed);
				g_logger_info("World seed (as float): %2.8f", World::seedAsFloat.load());
			}
			break;
			case NetworkEventType::EntityData:
			{
				Ecs::Registry* registry = Scene::getRegistry();
				RawMemory memory;
				memory.data = data;
				memory.size = event->dataSize;
				registry->deserialize(memory);
			}
			break;
			case NetworkEventType::LocalPlayer:
			{
				Ecs::EntityId localPlayer = *(Ecs::EntityId*)data;
				World::setLocalPlayer(localPlayer);
				PlayerController::setPlayerIfNeeded(true);
				isConnectingVar = false;

				// Once we get all the initial data, sync up with the server time
				uint64 myTime = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()
					).count();
				SizedMemory memory = pack<uint64, uint64, uint64>(myTime, 0, 0);
				Network::sendClientCommand(ClientCommandType::ServerTime, memory);
				g_memory_free(memory.memory);
			}
			break;
			case NetworkEventType::UserCommand:
			{
				UserCommand* command = (UserCommand*)data;
				uint8* userCommandData = (uint8*)data + sizeof(UserCommand);
				processUserCommand(command, userCommandData);
			}
			break;
			case NetworkEventType::ClientCommand:
			{
				ClientCommand* command = (ClientCommand*)data;
				uint8* clientCommandData = (uint8*)data + sizeof(ClientCommand);
				processClientCommand(command, clientCommandData);
			}
			break;
			default:
			{
				g_logger_error("<Client> Unknown NetworkEventType in client: '%s'", magic_enum::enum_name(event->type).data());
			}
			break;
			}
		}

		static void processUserCommand(UserCommand* command, void* userCommandData)
		{
			switch (command->type)
			{
			case UserCommandType::UpdateTransform:
			{
				UpdateTransformCommand bufferCommand;
				SizedMemory sizedData = SizedMemory{ (uint8*)userCommandData, command->sizeOfData };
				unpack<glm::vec3, glm::vec3, Ecs::EntityId>(
					sizedData,
					&bufferCommand.position,
					&bufferCommand.orientation,
					&bufferCommand.entity
					);
				bufferCommand.timestamp = command->timestamp;
				transformCommandBuffer.insert(bufferCommand);

				// Use a command from at least 300ms ago
				bool foundPosition;
				glm::vec3 position, orientation;
				if (transformCommandBuffer.predict(lagInMs, bufferCommand.entity, &position, &orientation))
				{
					Ecs::Registry* registry = Scene::getRegistry();
					if (registry->hasComponent<Transform>(bufferCommand.entity))
					{
						Transform& transform = registry->getComponent<Transform>(bufferCommand.entity);
						transform.position = position;
						transform.orientation = orientation;
					}
				}
			}
			break;
			default:
				g_logger_error("<Client> Unknown user command '%s'.", magic_enum::enum_name(command->type).data());
				break;
			}
		}

		static void processClientCommand(ClientCommand* command, void* clientCommandData)
		{
			switch (command->type)
			{
			case ClientCommandType::Give:
			{
				int blockId, blockCount;
				Ecs::EntityId player;
				unpack<int, int, Ecs::EntityId>(
					SizedMemory{ (uint8*)clientCommandData, command->sizeOfData },
					&blockId,
					&blockCount,
					&player
					);

				// TODO: Add buffering here. Buffer the commands so you can perform interpolation of updates client side
				Ecs::Registry* registry = Scene::getRegistry();
				if (player != Ecs::nullEntity)
				{
					World::givePlayerBlock(player, blockId, blockCount);
				}
			}
			break;
			case ClientCommandType::SetBlock:
			{
				glm::vec3 worldPosition;
				Block block;
				SizedMemory sizedData = SizedMemory{ (uint8*)clientCommandData, command->sizeOfData };
				unpack<glm::vec3, Block>(
					sizedData,
					&worldPosition,
					&block
					);

				// TODO: Add buffering here. Buffer the commands so you can perform interpolation of updates client side
				ChunkManager::setBlock(worldPosition, block);
			}
			break;
			case ClientCommandType::RemoveBlock:
			{
				glm::vec3 worldPosition;
				SizedMemory sizedData = SizedMemory{ (uint8*)clientCommandData, command->sizeOfData };
				unpack<glm::vec3>(
					sizedData,
					&worldPosition
					);

				// TODO: Add buffering here. Buffer the commands so you can perform interpolation of updates client side
				ChunkManager::removeBlock(worldPosition);
			}
			break;
			case ClientCommandType::Chat:
			{
				// TODO: Do cheat checking, make sure the entity hasn't moved farther than it should in one update
				// TODO: Add buffering here. Buffer the commands so you can perform interpolation of updates client side'
				SizedMemory sizedData = SizedMemory{ (uint8*)clientCommandData, command->sizeOfData };
				char* message = (char*)clientCommandData;
				size_t strLength = std::strlen(message) + 1;

				// TODO: Make sure I'm not going out of bounds memory here
				Ecs::EntityId player = *(Ecs::EntityId*)(sizedData.memory + (strLength * sizeof(char)));

				Ecs::Registry* registry = Scene::getRegistry();
				if (player != Ecs::nullEntity && registry->hasComponent<PlayerComponent>(player))
				{
					const PlayerComponent& playerComponent = registry->getComponent<PlayerComponent>(player);

					MainHud::generalMessage(player, message);
				}
			}
			break;
			case ClientCommandType::ServerTime:
			{
				uint64 myTimePacketSent, serverTime, serverGameTime;
				SizedMemory sizedData = SizedMemory{ (uint8*)clientCommandData, command->sizeOfData };
				unpack<uint64, uint64, uint64>(
					sizedData,
					&myTimePacketSent,
					&serverTime,
					&serverGameTime
					);

				// Server time drift is approximately half the ping time
				uint64 myTimeNow = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()
					).count();
				serverTimeDrift = (myTimeNow - myTimePacketSent) / 2;
				clientGameTime = serverGameTime - serverTimeDrift;
				g_logger_info("Client time drift is %u", serverTimeDrift);
			}
			break;
			case ClientCommandType::Handshake:
			{
				SizedMemory myName;
				myName.size = (World::localPlayerName.size() + 1) * sizeof(char);
				myName.memory = (uint8*)g_memory_allocate(myName.size);
				std::strcpy((char*)myName.memory, World::localPlayerName.c_str());
				myName.memory[World::localPlayerName.size()] = '\0';
				g_logger_info("Client '%s' responding to handshake.", (char*)myName.memory);
				Network::sendClientCommand(ClientCommandType::ClientLoadInfo, myName);
				g_memory_free(myName.memory);
			}
			break;
			case ClientCommandType::CalculateLighting:
			{
				glm::vec3 lastPlayerPos;
				SizedMemory sizedData = SizedMemory{ (uint8*)clientCommandData, command->sizeOfData };
				unpack<glm::vec3>(
					sizedData,
					&lastPlayerPos
				);

				glm::ivec2 playerPosChunkCoords = World::toChunkCoords(lastPlayerPos);
				ChunkManager::setPlayerChunkPos(playerPosChunkCoords);
				ChunkManager::queueCalculateLighting(playerPosChunkCoords);
			}
			break;
			case ClientCommandType::SetTime:
			{
				int time;
				SizedMemory sizedData = SizedMemory{ (uint8*)clientCommandData, command->sizeOfData };
				unpack<int>(
					sizedData,
					&time
					);

				World::worldTime = time;
			}
			break;
			default:
			{
				g_logger_error("<Client> Unknown client command '%s'.", magic_enum::enum_name(command->type).data());
			}
			break;
			}
		}
	}
}
