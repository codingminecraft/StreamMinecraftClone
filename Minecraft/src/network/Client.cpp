#include "network/Client.h"
#include "core.h"
#include "core/Scene.h"
#include "network/Network.h"
#include "world/ChunkManager.h"
#include "world/Chunk.hpp"
#include "world/BlockMap.h"
#include "gameplay/PlayerController.h"
#include "core/Components.h"

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

		static const char* hostname;
		static int port;

		// Internal functions
		static void processEvent(NetworkEvent* event, uint8* data);
		static void processUserCommand(UserCommand* command, void* userCommandData);
		static void processClientCommand(ClientCommand* command, void* userCommandData);

		void init(const char* inHostname, int inPort)
		{
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

			// Bind the client to the default localhost
			g_logger_assert(strcmp(inHostname, "") != 0 && inPort != 0, "Need to supply hostname and port to server initialization.");
			hostname = inHostname;
			port = inPort;

			enet_address_set_host(&address, hostname);
			address.port = port;

			// Create a peer that is connected to the server
			peer = enet_host_connect(client, &address, 1, 0);
			if (peer == NULL)
			{
				g_logger_assert(false, "No available peers for initiating an ENet connection.");
				return;
			}

			// Check if the connection succeeded
			ENetEvent event;
			if (enet_host_service(client, &event, 15000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
			{
				g_logger_info("Successfully connected to '%s':'%d'", hostname, port);
				event.peer->data = (void*)"Client Info Blah";
			}
			else
			{
				enet_peer_reset(peer);
				g_logger_error("Failed to connect to '%s':'%d'", hostname, port);
				peer = NULL;
				free();
				return;
			}
		}

		void update()
		{
			ENetEvent event;

			// Process all events
			while (enet_host_service(client, &event, 0) > 0)
			{
				switch (event.type)
				{
				case ENET_EVENT_TYPE_CONNECT:
				{
					g_logger_info("A new client connected from %x:%u.", event.peer->address.host, event.peer->address.port);
					// Store any relevant client information here...
					event.peer->data = (void*)"Client information";
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

			enet_host_destroy(client);
			client = NULL;

			hostname = "";
			port = 0;
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
						for (int blockCounter = 0; blockCounter < maxBlockCount; blockCounter++)
						{
							chunkData[blockIndex].id = blockId;
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
				g_logger_info("Done processing chunk data.");
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
			case UserCommandType::UpdatePosition:
			{
				glm::vec3 newPosition;
				Ecs::EntityId entityId;
				unpack<glm::vec3, Ecs::EntityId>(
					SizedMemory{ (uint8*)userCommandData, command->sizeOfData },
					&newPosition,
					&entityId
					);

				// TODO: Add interpolation and buffering here so that the client is not snapping objects into position
				// every update
				Ecs::Registry* registry = Scene::getRegistry();
				if (registry->hasComponent<Transform>(entityId))
				{
					registry->getComponent<Transform>(entityId).position = newPosition;
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
					g_logger_info("<%s>: %s", playerComponent.name, message);
				}
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
