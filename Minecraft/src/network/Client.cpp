#include "network/Client.h"
#include "network/PositionCommandBuffer.h"
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

		static const char* hostname;
		static int port;

		static PositionCommandBuffer positionCommandBuffer;
		static constexpr uint64 lagInMs = 300;
		static constexpr int maxNumPositionCommands = 3000;

		// The time that this client is drifting from the server in milliseconds
		static uint64 serverTimeDrift;
		uint64 clientGameTime = 0;

		// Internal functions
		static void processEvent(NetworkEvent* event, uint8* data);
		static void processUserCommand(UserCommand* command, void* userCommandData);
		static void processClientCommand(ClientCommand* command, void* userCommandData);

		void init(const char*, int)
		{
			serverTimeDrift = 0;
			clientGameTime = 0;

			// Try to find a server to connect to 
			// Adapted from http://cxong.github.io/2016/01/how-to-write-a-lan-server
			ENetSocket scanner = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
			// We need to set a socket option in order to send to the broadcast address
			enet_socket_set_option(scanner, ENET_SOCKOPT_BROADCAST, 1);
			ENetAddress scannerAddress;
			scannerAddress.host = ENET_HOST_BROADCAST;
			scannerAddress.port = Server::listeningPort;
			// Send a dummy payload; you can make your own (larger) payload
			// but make sure to update the server code if you do
			char data = 0xBA;
			ENetBuffer sendbuf;
			sendbuf.data = &data;
			sendbuf.dataLength = 1;
			enet_socket_send(scanner, &scannerAddress, &sendbuf, 1);

			// Note that enet_socket_receive is blocking;
			// for a non-blocking version use enet_socketset_select to check before receiving
			enet_uint16 server_port;
			ENetBuffer recvbuf;
			recvbuf.data = &server_port;
			recvbuf.dataLength = sizeof(server_port);
			enet_socket_receive(scanner, &address, &recvbuf, 1);
			// If the message is correct, we should have received sizeof(enet_uint16) worth of data
			// Once again, error checking would be nice here, but omitted for brevity
			g_logger_assert(recvbuf.dataLength == sizeof(enet_uint16), "Invalid recv buffer on client side.");
			address.port = server_port;
			// Now addr holds the exact host/port to connect to

			// But first, shut down the scanner because we're done with it
			enet_socket_shutdown(scanner, ENET_SOCKET_SHUTDOWN_READ_WRITE);
			enet_socket_destroy(scanner);

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
			//hostname = addr.host;
			port = address.port;

			//enet_address_set_host(&address, hostname);
			//address.port = port;

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

			positionCommandBuffer.init(maxNumPositionCommands);
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
			positionCommandBuffer.free();

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
			case UserCommandType::UpdatePosition:
			{
				UpdatePositionCommand bufferCommand;
				SizedMemory sizedData = SizedMemory{ (uint8*)userCommandData, command->sizeOfData };
				unpack<glm::vec3, Ecs::EntityId>(
					sizedData,
					&bufferCommand.position,
					&bufferCommand.entity
					);
				bufferCommand.timestamp = command->timestamp;
				positionCommandBuffer.insert(bufferCommand);

				// TODO: Do cheat checking, make sure the entity hasn't moved farther than it should in one update
				// Use a command from at least 100ms ago
				bool foundPosition;
				glm::vec3 position = positionCommandBuffer.predict(lagInMs, bufferCommand.entity, &foundPosition);
				if (foundPosition)
				{
					Ecs::Registry* registry = Scene::getRegistry();
					registry->getComponent<Transform>(bufferCommand.entity).position = position;
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
			default:
			{
				g_logger_error("<Client> Unknown client command '%s'.", magic_enum::enum_name(command->type).data());
			}
			break;
			}
		}
	}
}
