#include "network/Server.h"
#include "network/PositionCommandBuffer.h"
#include "core.h"
#include "core/Scene.h"
#include "core/Components.h"
#include "network/Network.h"
#include "world/ChunkManager.h"
#include "world/Chunk.hpp"
#include "world/BlockMap.h"
#include "gameplay/CharacterController.h"
#include "gameplay/PlayerController.h"
#include "gui/MainHud.h"

#include <enet/enet.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace Minecraft
{
	namespace Server
	{
		// Internal variables
		static ENetAddress address;
		static ENetHost* server;
		static const int maxClients = 32;
		static std::array<ENetPeer*, maxClients> clients;
		static int numConnectedClients;

		static ENetSocket listenSocket;
		static ENetAddress listenAddress;

		static const char* hostname = nullptr;
		static int port = 0;

		// Time since server started in milliseconds
		uint64 serverGameTime;

		// Internal functions
		static void checkForClientBroadcasts();
		static void processEvent(NetworkEvent* event, uint8* data, ENetPeer* peer);
		static void processUserCommand(UserCommand* command, void* userCommandData, ENetPeer* peer);
		static void processClientCommand(ClientCommand* command, void* userCommandData, ENetPeer* peer);
		// TODO: Should there be server commands? If so, what's the difference from a ClientCommand?
		// static void processServerCommand(UserCommand* command, void* userCommandData, ENetPeer* peer);

		// Internal buffers
		static PositionCommandBuffer positionCommandBuffer;
		static constexpr uint64 lagInMs = 300;
		static constexpr int maxNumPositionCommands = 3000;

		// Tmp
		static const char* serverPlayerName = "ExternalClientPlayer";

		void init(const char* inHostname, int inPort)
		{
			g_logger_assert(strcmp(inHostname, "") != 0 && inPort != 0, "Need to supply hostname and port to server initialization.");
			hostname = inHostname;
			port = inPort;
			serverGameTime = 0;

			// First start the listening socket, this will listen and respond to anybody looking for a server on LAN
			// This code is adapted from: http://cxong.github.io/2016/01/how-to-write-a-lan-server
			listenSocket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
			// Allow the port to be reused by other servers
			enet_socket_set_option(listenSocket, ENET_SOCKOPT_REUSEADDR, 1);
			listenAddress.host = ENET_HOST_ANY;
			listenAddress.port = listeningPort;
			enet_socket_bind(listenSocket, &listenAddress);

			// Bind the server to the default localhost
			g_logger_warning("Server only supports localhost right now.");
			address.host = ENET_HOST_ANY;
			address.port = port;
			g_memory_zeroMem(clients.data(), sizeof(ENetPeer*) * maxClients);
			numConnectedClients = 0;

			server = enet_host_create(
				&address, // the address to bind the server host to
				32, // allow up to 32 clients and/or outgoing connections
				2, // allow up to 2 channels to be used, 0 and 1
				0, // assume any amount of incoming bandwidth
				0); // assume any amount of outgoing bandwidth

			if (server == NULL)
			{
				g_logger_assert(false, "An error occurred while trying to create an ENet server host.");
				return;
			}

			positionCommandBuffer.init(maxNumPositionCommands);
		}

		void update()
		{
			serverGameTime += (uint64)(World::deltaTime * 1000.0f);
			checkForClientBroadcasts();

			ENetEvent event;

			// Process all events
			while (enet_host_service(server, &event, 0) > 0)
			{
				switch (event.type)
				{
				case ENET_EVENT_TYPE_CONNECT:
				{
					g_logger_info("A new client connected from %x:%u.", event.peer->address.host, event.peer->address.port);
					// Store any relevant client information here...
					event.peer->data = (void*)"Client information";
					clients[numConnectedClients] = event.peer;
					numConnectedClients++;
					g_logger_assert(numConnectedClients <= 32, "Somehow we connected more than the maximum number of clients allowed.");

					g_logger_info("Sending client chunk data.");
					robin_hood::unordered_node_map<glm::ivec2, Chunk>& chunks = ChunkManager::getAllChunks();
					Network::sendClient(event.peer, NetworkEventType::WorldSeed, &World::seed, sizeof(uint32));
					uint16 numChunks = (uint16)chunks.size();
					size_t chunkDataSize = sizeof(Block) * World::ChunkHeight * World::ChunkWidth * World::ChunkDepth;
					size_t chunkCoordsSize = sizeof(int32) * 2;
					size_t chunkStateSize = sizeof(ChunkState);
					size_t chunkCompressedSizeSize = sizeof(uint32);
					uint8* chunkDataEvent = (uint8*)g_memory_allocate(sizeof(uint16) + (chunkDataSize * numChunks) + (chunkCoordsSize * numChunks) + (chunkStateSize * numChunks));
					g_logger_info("Num chunks: %u", numChunks);
					g_memory_copyMem(chunkDataEvent, &numChunks, sizeof(uint16));
					uint8* chunkDataPtr = chunkDataEvent + sizeof(uint16);
					bool first = true;
					for (auto& pair : chunks)
					{
						uint32 compressedChunkSize = 0;
						Chunk& chunk = pair.second;
						if (chunk.state == ChunkState::Loaded)
						{
							// Compressed chunk looks like this
							// NumChunks -> ChunkSize (uint16) -> blockId (uint16) -> blockCount(uint16) -> ... ...
							//		   -> blockId(uint16) -> blockCount(uint16)
							//         -> chunkCoords (int32) * 2 -> chunkState (uint8)
							uint8* chunkDataCurrentPtr = chunkDataPtr + chunkCompressedSizeSize;
							uint16 lastBlockId = chunk.data[0].id;
							uint16 lastBlockCount = 1;
							for (uint32 i = 1; i < World::ChunkHeight * World::ChunkWidth * World::ChunkDepth; i++)
							{
								if (chunk.data[i].id != lastBlockId)
								{
									// Write to our memory
									g_memory_copyMem(chunkDataCurrentPtr, &lastBlockId, sizeof(uint16));
									chunkDataCurrentPtr += sizeof(uint16);
									g_memory_copyMem(chunkDataCurrentPtr, &lastBlockCount, sizeof(uint16));
									chunkDataCurrentPtr += sizeof(uint16);
									compressedChunkSize += (sizeof(uint16) * 2);

									// Set the next id
									lastBlockId = chunk.data[i].id;
									lastBlockCount = 0;
								}
								lastBlockCount++;
							}
							if (lastBlockCount > 0)
							{
								g_memory_copyMem(chunkDataCurrentPtr, &lastBlockId, sizeof(uint16));
								chunkDataCurrentPtr += sizeof(uint16);
								g_memory_copyMem(chunkDataCurrentPtr, &lastBlockCount, sizeof(uint16));
								chunkDataCurrentPtr += sizeof(uint16);
								compressedChunkSize += (sizeof(uint16) * 2);
							}
							g_memory_copyMem(chunkDataPtr, &compressedChunkSize, sizeof(uint32));
							chunkDataPtr += chunkCompressedSizeSize + compressedChunkSize;

							g_memory_copyMem(chunkDataPtr, &chunk.chunkCoords.x, sizeof(int32));
							chunkDataPtr += sizeof(int32);
							g_memory_copyMem(chunkDataPtr, &chunk.chunkCoords.y, sizeof(int32));
							chunkDataPtr += sizeof(int32);
							g_memory_copyMem(chunkDataPtr, &chunk.state, sizeof(ChunkState));
							chunkDataPtr += sizeof(ChunkState);
						}
					}
					size_t totalCompressedSize = chunkDataPtr - chunkDataEvent;
					g_logger_info("Total compressed chunk data size: %u bytes", totalCompressedSize);
					Network::sendClient(event.peer, NetworkEventType::ChunkData, chunkDataEvent, totalCompressedSize);
					g_memory_free(chunkDataEvent);

					g_logger_info("Telling client to patch their dang chunk neighbors.");
					Network::sendClient(event.peer, NetworkEventType::PatchChunkNeighbors, nullptr, 0);
					Network::sendClient(event.peer, NetworkEventType::NotifyChunkWorker, nullptr, 0);

					Ecs::Registry* registry = Scene::getRegistry();
					Ecs::EntityId currentPlayer = registry->find(TagType::Player);
					Transform& currentPlayerTransform = registry->getComponent<Transform>(currentPlayer);
					// Check if we need a new player or if the player has joined before
					Ecs::EntityId newPlayer = Ecs::nullEntity;
					for (auto entity : registry->view<PlayerComponent>())
					{
						const PlayerComponent& playerComponent = registry->getComponent<PlayerComponent>(entity);
						if (std::strcmp(playerComponent.name, serverPlayerName) == 0)
						{
							newPlayer = entity;
							break;
						}
					}
					if (newPlayer == Ecs::nullEntity)
					{
						newPlayer = World::createPlayer(serverPlayerName, currentPlayerTransform.position);
						g_logger_info("Welcome '%s'. First time joining this world.", serverPlayerName);
					}
					registry->getComponent<CharacterController>(newPlayer).lockedToCamera = false;
					// Synchronize the ECS
					RawMemory entityMemory = registry->serialize();
					Network::broadcast(NetworkEventType::EntityData, entityMemory.data, entityMemory.size);
					// Then set the new local player
					Network::broadcast(NetworkEventType::LocalPlayer, &newPlayer, sizeof(Ecs::EntityId));
					g_memory_free(entityMemory.data);
					enet_host_flush(server);
				}
				break;
				case ENET_EVENT_TYPE_RECEIVE:
				{
					//g_logger_info("A packet of length %u containing %s was received from %s on channel %u.",
					//	event.packet->dataLength,
					//	event.packet->data,
					//	event.peer->data,
					//	event.channelID);
					NetworkEventData networkEventData = Network::deserializeNetworkEvent(event.packet->data, event.packet->dataLength);
					processEvent(networkEventData.event, networkEventData.data, event.peer);

					// Clean up the packet now that we're done using it
					enet_packet_destroy(event.packet);
				}
				break;
				case ENET_EVENT_TYPE_DISCONNECT:
				{
					g_logger_info("%s disconnected.", event.peer->data);

					bool foundClient = false;
					for (int i = 0; i < clients.size(); i++)
					{
						if (clients[i] == event.peer)
						{
							foundClient = true;
							clients[i] = clients[numConnectedClients - 1];
							clients[numConnectedClients - 1] = NULL;
							numConnectedClients--;
							break;
						}
					}

					// Reset the peer's client information
					event.peer->data = NULL;
				}
				break;
				}
			}
		}

		void broadcast(ENetPacket* packet)
		{
			//enet_host_broadcast(server, 0, packet);
			for (int i = 0; i < numConnectedClients; i++)
			{
				sendClient(clients[i], packet);
			}
		}

		void sendClient(ENetPeer* peer, ENetPacket* packet)
		{
			if (enet_peer_send(peer, 0, packet) != 0)
			{
				g_logger_error("Failed to send packet from server.");
			}
		}

		void free()
		{
			enet_socket_shutdown(listenSocket, ENET_SOCKET_SHUTDOWN_READ_WRITE);
			enet_socket_destroy(listenSocket);
			enet_host_destroy(server);
			positionCommandBuffer.free();

			hostname = "";
			port = 0;
		}

		static void checkForClientBroadcasts()
		{
			// Look for anybody that broadcast asking to see servers
			ENetSocketSet set;
			ENET_SOCKETSET_EMPTY(set);
			ENET_SOCKETSET_ADD(set, listenSocket);
			if (enet_socketset_select(listenSocket, &set, NULL, 0) <= 0) return;

			// Construct a data buffer (ENetBuffer) to receive the data
			// Because we know exactly how big the scan packets will be (1 byte),
			// we'll only set aside enough memory for that.
			// If you want bigger payloads, adjust this to suit.
			// If you want dynamic payloads, or you want to receive any payload just for the heck of it,
			// use a suitably big buffer.
			ENetAddress addr;
			char buf;
			ENetBuffer recvbuf;
			recvbuf.data = &buf;
			recvbuf.dataLength = 1;
			if (enet_socket_receive(listenSocket, &addr, &recvbuf, 1) <= 0) return;

			if (buf != (char)0xBA)
			{
				g_logger_warning("Recieved invalid message on the broadcast port.");
				return;
			}

			// Reply to scanner client with the port of the server host
			// We know the client address from the enet_socket_receive function
			ENetBuffer replybuf;
			replybuf.data = &server->address.port;
			replybuf.dataLength = sizeof(server->address.port);
			enet_socket_send(listenSocket, &addr, &replybuf, 1);
		}

		static void processEvent(NetworkEvent* event, uint8* data, ENetPeer* peer)
		{
			switch (event->type)
			{
			case NetworkEventType::UserCommand:
			{
				UserCommand* command = (UserCommand*)data;
				size_t sizeOfCommand = sizeof(UserCommand) + command->sizeOfData;
				void* userCommandData = (void*)((UserCommand*)data + 1);
				processUserCommand(command, userCommandData, peer);
			}
			break;
			case NetworkEventType::ClientCommand:
			{
				ClientCommand* command = (ClientCommand*)data;
				size_t sizeOfCommand = sizeof(ClientCommand) + command->sizeOfData;
				void* userCommandData = (void*)((ClientCommand*)data + 1);
				processClientCommand(command, userCommandData, peer);
			}
			break;
			default:
			{
				g_logger_error("<Server> Unknown chat NetworkEventType: %d", event->type);
			}
			break;
			}
		}

		static void processUserCommand(UserCommand* command, void* userCommandData, ENetPeer* peer)
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

				// TODO: Don't broadcast these back to all the clients every update, it's stupid
				// instead, buffer the data and send bulk updates and perform interpolation
				for (int i = 0; i < numConnectedClients; i++)
				{
					ENetPeer* peerToSendTo = clients[i];
					if (peerToSendTo != peer)
					{
						Network::sendUserCommand(command->type, sizedData, peerToSendTo);
					}
				}

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
				g_logger_error("<Server> Unknown user command '%s'.", magic_enum::enum_name(command->type).data());
				break;
			}
		}

		static void processClientCommand(ClientCommand* command, void* clientCommandData, ENetPeer* peer)
		{
			switch (command->type)
			{
			case ClientCommandType::Give:
			{
				int blockId, blockCount;
				Ecs::EntityId player;
				SizedMemory sizedData = SizedMemory{ (uint8*)clientCommandData, command->sizeOfData };
				unpack<int, int, Ecs::EntityId>(
					sizedData,
					&blockId,
					&blockCount,
					&player
					);

				// TODO: Do cheat checking, make sure the entity hasn't moved farther than it should in one update
				// TODO: Add buffering here. Buffer the commands so you can perform interpolation of updates client side
				Ecs::Registry* registry = Scene::getRegistry();
				if (player != Ecs::nullEntity)
				{
					World::givePlayerBlock(player, blockId, blockCount);

					for (int i = 0; i < numConnectedClients; i++)
					{
						// This is a 2-way event, because the server must verify this action is not cheating
						// So we want to send it back to everyone including the client that issued the command
						ENetPeer* peerToSendTo = clients[i];
						Network::sendClientCommand(command->type, sizedData, peerToSendTo);
					}
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

				// TODO: Do cheat checking, make sure the entity hasn't moved farther than it should in one update
				// TODO: Add buffering here. Buffer the commands so you can perform interpolation of updates client side
				ChunkManager::setBlock(worldPosition, block);

				for (int i = 0; i < numConnectedClients; i++)
				{
					ENetPeer* peerToSendTo = clients[i];
					if (peerToSendTo != peer)
					{
						// This is a 2-way event, because the server must verify this action is not cheating
						// So we want to send it back to everyone including the client that issued the command
						ENetPeer* peerToSendTo = clients[i];
						Network::sendClientCommand(command->type, sizedData, peerToSendTo);
					}
				}
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

				// TODO: Do cheat checking, make sure the entity hasn't moved farther than it should in one update
				// TODO: Add buffering here. Buffer the commands so you can perform interpolation of updates client side
				ChunkManager::removeBlock(worldPosition);

				for (int i = 0; i < numConnectedClients; i++)
				{
					ENetPeer* peerToSendTo = clients[i];
					if (peerToSendTo != peer)
					{
						// This is a 2-way event, because the server must verify this action is not cheating
						// So we want to send it back to everyone including the client that issued the command
						ENetPeer* peerToSendTo = clients[i];
						Network::sendClientCommand(command->type, sizedData, peerToSendTo);
					}
				}
			}
			break;
			case ClientCommandType::Chat:
			{
				// TODO: Do cheat checking, make sure the entity hasn't moved farther than it should in one update
				// TODO: Add buffering here. Buffer the commands so you can perform interpolation of updates client side
				SizedMemory sizedData = SizedMemory{ (uint8*)clientCommandData, command->sizeOfData };
				char* message = (char*)clientCommandData;
				size_t strLength = std::strlen(message) + 1;
				Ecs::EntityId player = *(Ecs::EntityId*)(sizedData.memory + (strLength * sizeof(char)));

				Ecs::Registry* registry = Scene::getRegistry();
				if (player != Ecs::nullEntity && registry->hasComponent<PlayerComponent>(player))
				{
					const PlayerComponent& playerComponent = registry->getComponent<PlayerComponent>(player);
					MainHud::generalMessage(player, message);

					for (int i = 0; i < numConnectedClients; i++)
					{
						ENetPeer* peerToSendTo = clients[i];
						if (peerToSendTo != peer)
						{
							// This is a 2-way event, because the server must verify this action is not cheating
							// So we want to send it back to everyone including the client that issued the command
							ENetPeer* peerToSendTo = clients[i];
							Network::sendClientCommand(command->type, sizedData, peerToSendTo);
						}
					}
				}
			}
			break;
			case ClientCommandType::ServerTime:
			{
				uint64 clientTime, tmp1, tmp2;
				SizedMemory sizedData = SizedMemory{ (uint8*)clientCommandData, command->sizeOfData };
				unpack<uint64, uint64, uint64>(
					sizedData,
					&clientTime,
					&tmp1,
					&tmp2
					);

				uint64 myTime = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()
				).count();
				SizedMemory responseMessage = pack<uint64, uint64, uint64>(clientTime, myTime, serverGameTime);
				Network::sendClientCommand(ClientCommandType::ServerTime, responseMessage, peer);
				g_memory_free(responseMessage.memory);
			}
			break;
			default:
				g_logger_error("<Server> Unknown client command '%s'.", magic_enum::enum_name(command->type).data());
				break;
			}
		}
	}
}