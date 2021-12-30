#include "gui/LanServerMenu.h"
#include "gui/Gui.h"
#include "gui/GuiElements.h"
#include "gui/MainMenu.h"
#include "core/Scene.h"
#include "core/AppData.h"
#include "core/File.h"
#include "world/BlockMap.h"
#include "world/World.h"
#include "renderer/Styles.h"
#include "renderer/Renderer.h"
#include "network/Server.h"
#include "network/Network.h"
#include "network/Client.h"

#include <enet/enet.h>

namespace Minecraft
{
	struct ServerConnectionPoint
	{
		ENetAddress address;
		char* serverName;
		int numPlayers;
		char** playerNames;

		void free()
		{
			if (serverName)
			{
				g_memory_free(serverName);
			}

			if (playerNames)
			{
				for (int i = 0; i < numPlayers; i++)
				{
					if (playerNames[i])
					{
						g_memory_free(playerNames[i]);
					}
				}
				g_memory_free(playerNames);
			}
		}
	};

	namespace LanServerMenu
	{
		// Internal variables
		static bool isJoiningAServer;
		static Sprite dirtTextureSprite;
		static std::vector<ServerConnectionPoint> servers;
		static int selectedServerIndex;
		static int selectedPlayerName;
		static Sprite nullSprite;
		static int sendBroadcastCount;
		static int secondCount;
		static constexpr int maxRetries = 10;

		static ENetSocket scanner;
		static ENetAddress scannerAddress;

		// Internal functions
		static void showServers();
		static void showJoinServerScreen();
		static void checkForValidServers();
		static void sendBroadcast();

		void init()
		{
			resetState();

			g_logger_info("Initialized settings menu.");

			dirtTextureSprite.texture = TextureBuilder()
				.setFilepath("assets/images/block/dirt.png")
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setTextureType(TextureType::_2D)
				.setWrapS(WrapMode::Repeat)
				.setWrapT(WrapMode::Repeat)
				.generateTextureObject()
				.bindTextureObject()
				.generate(true);
			dirtTextureSprite.uvStart = glm::vec2(0.0f, 0.0f);
			dirtTextureSprite.uvSize = glm::vec2(5.0f, 3.0f);

			if (enet_initialize() != 0)
			{
				g_logger_assert(false, "An error occurred while initializing ENet.");
			}

			// Try to find any servers on LAN
			// Adapted from http://cxong.github.io/2016/01/how-to-write-a-lan-server
			scanner = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
			// We need to set a socket option in order to send to the broadcast address
			enet_socket_set_option(scanner, ENET_SOCKOPT_BROADCAST, 1);
			scannerAddress.host = ENET_HOST_BROADCAST;
			scannerAddress.port = Server::listeningPort;
			sendBroadcast();
		}

		void resetState()
		{
			sendBroadcastCount = 0;
			secondCount = 0;
			for (auto server : servers)
			{
				server.free();
			}
			servers.clear();

			nullSprite.texture.graphicsId = UINT32_MAX;
			isJoiningAServer = false;
			selectedServerIndex = -1;
			selectedPlayerName = -1;
		}

		void update()
		{
			sendBroadcastCount++;
			// Send a ping once a second for 15 seconds
			if (sendBroadcastCount >= 30 && secondCount < maxRetries)
			{
				sendBroadcast();
				sendBroadcastCount = 0;
				secondCount++;
			}
			checkForValidServers();

			static Style dirtStyle = Styles::defaultStyle;
			dirtStyle.color = "#232323ff"_hex;
			dirtTextureSprite.uvSize = glm::vec2(12.0f, 4.0f);
			Renderer::drawTexture2D(dirtTextureSprite, glm::vec2(-3.0f, -1.0f), glm::vec2(6.0f, 2.0f), dirtStyle, -3);
			dirtStyle.color = "#777777ff"_hex;
			dirtTextureSprite.uvSize = glm::vec2(12.0f, 6.0f);
			Renderer::drawTexture2D(dirtTextureSprite, glm::vec2(-3.0f, -1.5f), glm::vec2(6.0f, 3.0f), dirtStyle, -4);

			if (isJoiningAServer)
			{
				showJoinServerScreen();
			}
			else
			{
				showServers();
			}
		}

		void free()
		{
			dirtTextureSprite.texture.destroy();
			for (auto server : servers)
			{
				g_memory_free(server.serverName);
				for (int i = 0; i < server.numPlayers; i++)
				{
					g_memory_free(server.playerNames[i]);
				}
				g_memory_free(server.playerNames);
			}
			servers.clear();

			// But first, shut down the scanner because we're done with it
			enet_socket_shutdown(scanner, ENET_SOCKET_SHUTDOWN_READ_WRITE);
			enet_socket_destroy(scanner);
			enet_deinitialize();
		}

		static void showServers()
		{
			// Display connecting text if we're trying to connect
			if (servers.size() == 0 && secondCount < maxRetries)
			{
				std::string connectingText = "Connecting";
				static int numDots = 1;
				static int dotCounter = 0;
				dotCounter = (dotCounter + 1) % 30;
				if (dotCounter == 0)
				{
					numDots = (numDots + 1) % 3;
				}
				for (int i = 0; i <= numDots; i++)
				{
					connectingText += ".";
				}

				const Font* defaultFont = GuiElements::defaultButton->font;
				Renderer::drawString(connectingText, *defaultFont, glm::vec2(-0.5f, 0.0f), 0.0025f, Styles::defaultStyle, 2);
			}
			else if (servers.size() == 0 && secondCount >= maxRetries)
			{
				std::string connectionFailedMessage = "Could not find any servers on LAN";
				const Font* defaultFont = GuiElements::defaultButton->font;
				Renderer::drawString(connectionFailedMessage, *defaultFont, glm::vec2(-1.3f, 0.0f), 0.0025f, Styles::defaultStyle, 2);
			}

			// Window 1 holds all of the save files
			Gui::beginWindow(glm::vec2(-3.0f, 1.0f), glm::vec2(6.0f, 2.0f));
			Gui::advanceCursor(glm::vec2(0.0f, 0.1f));

			TexturedButton button = *GuiElements::defaultButton;

			int i = 0;
			for (auto& server : servers)
			{
				Gui::centerNextElement();
				button.text = server.serverName;
				button.size.y = 0.25f;
				if (Gui::selectableText(server.serverName, button.size, selectedServerIndex == i))
				{
					selectedServerIndex = i;
				}
				Gui::advanceCursor(glm::vec2(0.0f, 0.05f));

				i++;
			}

			Gui::endWindow();

			// Window 2, this holds the load world and new world buttons
			Gui::beginWindow(glm::vec2(-3.0f, -1.0f), glm::vec2(6.0f, 0.5f));
			button.text = "Connect to Server";
			button.size.x = 1.45f;
			button.size.y = 0.3f;
			// TODO: Make it so I can place this after the element I just drew
			Gui::advanceCursor(glm::vec2((6.0f - (button.size.x * 2.0f)) / 2.0f, (0.5f - button.size.y) / 2.0f));
			Gui::sameLine();
			if (Gui::textureButton(button, selectedServerIndex == -1))
			{
				isJoiningAServer = true;
			}

			Gui::advanceCursor(glm::vec2(0.05f, 0.0f));
			button.text = "Cancel";
			if (Gui::textureButton(button))
			{
				MainMenu::resetState();
			}

			Gui::endWindow();
		}

		static void showJoinServerScreen()
		{
			// Window 1 holds the player names
			Gui::beginWindow(glm::vec2(-1.5f, 1.0f), glm::vec2(3.0f, 2.0f));
			Gui::advanceCursor(glm::vec2(0.0f, 0.1f));

			static char newPlayerName[128];
			static bool playerNameFocused = false;
			if (Gui::input("Player Name: ", 0.0025f, newPlayerName, 128, &playerNameFocused, true))
			{
				World::localPlayerName = std::string(newPlayerName);
				selectedPlayerName = -1;
			}

			Gui::advanceCursor(glm::vec2(0.0f, 0.1f));

			TexturedButton button = *GuiElements::defaultButton;

			for (int i = 0; i < servers[selectedServerIndex].numPlayers; i++)
			{
				Gui::centerNextElement();
				button.text = servers[selectedServerIndex].playerNames[i];
				button.size.y = 0.25f;
				if (Gui::selectableText(button.text, button.size, selectedPlayerName == i))
				{
					selectedPlayerName = i;
					World::localPlayerName = std::string(button.text);
					g_memory_zeroMem(newPlayerName, sizeof(char) * 128);
				}
				Gui::advanceCursor(glm::vec2(0.0f, 0.05f));
			}

			Gui::endWindow();

			// Window 2, this holds the create world button
			Gui::beginWindow(glm::vec2(-3.0f, -1.0f), glm::vec2(6.0f, 0.5f));
			Gui::centerNextElement();
			Gui::advanceCursor(glm::vec2(0.0f, (0.5f - button.size.y) / 2.0f));
			button.text = "Join";
			if (Gui::textureButton(button, selectedPlayerName == -1 && newPlayerName[0] == '\0'))
			{
				g_memory_zeroMem(newPlayerName, sizeof(char) * 128);
				Client::setAddress(servers[selectedServerIndex].address);
				Scene::changeScene(SceneType::LocalLanGame);
			}

			Gui::endWindow();
		}

		static void checkForValidServers()
		{
			ENetSocketSet set;
			ENET_SOCKETSET_EMPTY(set);
			ENET_SOCKETSET_ADD(set, scanner);
			if (enet_socketset_select(scanner, &set, NULL, 0) <= 0) return;

			uint32 maxDataSize = 256 * sizeof(uint8);
			uint8* data = (uint8*)g_memory_allocate(maxDataSize);
			ENetBuffer receiveBuffer;
			receiveBuffer.data = data;
			receiveBuffer.dataLength = maxDataSize;

			ENetAddress newAddress;
			if (enet_socket_receive(scanner, &newAddress, &receiveBuffer, 1) <= 0) return;

			uint32 dataSize = *(uint32*)data;
			if (dataSize > 256)
			{
				g_logger_error("Sanity check failed. We somehow recieved corrupted data when listening on the socket.");
				return;
			}

			// If the message is correct, we should have received sizeof(enet_uint16) worth of data
			// Once again, error checking would be nice here, but omitted for brevity

			// Deserialize all the data
			ServerConnectionPoint newConnection;
			uint8* bufferPtr = data;
			bufferPtr += sizeof(uint32);
			newAddress.port = *(uint16*)bufferPtr;
			bufferPtr += sizeof(uint16);
			int serverNameLength = std::strlen((char*)bufferPtr) + 1;
			newConnection.serverName = (char*)g_memory_allocate(sizeof(char) * serverNameLength);
			std::strcpy(newConnection.serverName, (char*)bufferPtr);
			newConnection.serverName[serverNameLength - 1] = '\0';
			bufferPtr += sizeof(char) * serverNameLength;
			newConnection.numPlayers = *(int*)bufferPtr;
			bufferPtr += sizeof(int);
			newConnection.playerNames = (char**)g_memory_allocate(sizeof(char**) * newConnection.numPlayers);

			for (int i = 0; i < newConnection.numPlayers; i++)
			{
				int strLength = std::strlen((char*)bufferPtr) + 1;
				newConnection.playerNames[i] = (char*)g_memory_allocate(sizeof(char) * strLength);
				std::strcpy(newConnection.playerNames[i], (char*)bufferPtr);
				*(char*)(newConnection.playerNames[i][strLength - 1]) = '\0';
				bufferPtr += sizeof(char) * strLength;
			}
			g_memory_free(data);

			// Now addr holds the exact host / port to connect to
			newConnection.address = newAddress;
			bool isNewConnection = true;
			for (auto& connection : servers)
			{
				// If we find this server already in our list, this is not a new connection
				if (std::strcmp(connection.serverName, newConnection.serverName) == 0)
				{
					isNewConnection = false;
					newConnection.free();
				}
			}

			if (isNewConnection)
			{
				servers.push_back(newConnection);
			}
		}

		static void sendBroadcast()
		{
			// CAFED00D is the magic number
			char data[4] = { 0xCA, 0xFE, 0XD0, 0x0D };
			ENetBuffer sendbuf;
			sendbuf.data = &data;
			sendbuf.dataLength = 4;
			enet_socket_send(scanner, &scannerAddress, &sendbuf, 1);
		}
	}
}