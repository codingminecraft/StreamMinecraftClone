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

#include <enet/enet.h>

namespace Minecraft
{
	struct ServerConnectionPoint
	{
		ENetAddress address;
		char* serverName;
		int numPlayers;
		char** playerNames;
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

		static ENetSocket scanner;
		static ENetAddress scannerAddress;

		// Internal functions
		static void showServers();
		static void showJoinServerScreen();

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

			// Try to find any servers on LAN
			// Adapted from http://cxong.github.io/2016/01/how-to-write-a-lan-server
			scanner = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
			// We need to set a socket option in order to send to the broadcast address
			enet_socket_set_option(scanner, ENET_SOCKOPT_BROADCAST, 1);
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
			//enet_uint16 server_port;
			//ENetBuffer recvbuf;
			//recvbuf.data = &server_port;
			//recvbuf.dataLength = sizeof(server_port);
			//enet_socket_receive(scanner, &address, &recvbuf, 1);
			//// If the message is correct, we should have received sizeof(enet_uint16) worth of data
			//// Once again, error checking would be nice here, but omitted for brevity
			//g_logger_assert(recvbuf.dataLength == sizeof(enet_uint16), "Invalid recv buffer on client side.");
			//address.port = server_port;
			// Now addr holds the exact host/port to connect to
		}

		void resetState()
		{
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

			nullSprite.texture.graphicsId = UINT32_MAX;
			isJoiningAServer = false;
			selectedServerIndex = -1;
			selectedPlayerName = -1;

			for (int i = 0; i < 3; i++)
			{
				ServerConnectionPoint fake;
				fake.serverName = (char*)g_memory_allocate(sizeof(char) * 20);
				std::string serverNameStr = std::string("Server") + std::to_string(i);
				std::strcpy(fake.serverName, serverNameStr.c_str());
				fake.serverName[7] = '\0';

				fake.numPlayers = 4;
				fake.playerNames = (char**)g_memory_allocate(sizeof(char*) * 4);
				for (int i = 0; i < 4; i++)
				{
					fake.playerNames[i] = (char*)g_memory_allocate(sizeof(char) * 20);
					std::string playerName;
					switch (i)
					{
					case 0:
						playerName = "Dumbpoop";
						break;
					case 1:
						playerName = "Mike";
						break;
					case 2:
						playerName = "Gabe";
						break;
					case 3:
						playerName = "NiBud";
						break;
					}
					std::strcpy(fake.playerNames[i], playerName.c_str());
					fake.playerNames[i][playerName.length()] = '\0';
				}
				servers.push_back(fake);
			}
		}

		void update()
		{
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
		}

		static void showServers()
		{
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
				//playerName = std::string(worldSaveTitle);
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
				//Scene::changeScene(SceneType::SinglePlayerGame);
			}

			Gui::endWindow();
		}
	}
}