#include "gameplay/CommandLine.h"
#include "renderer/Font.h"
#include "renderer/Styles.h"
#include "renderer/Renderer.h"
#include "gui/Gui.h"
#include "gui/MainHud.h"
#include "input/Input.h"
#include "world/BlockMap.h"
#include "core/Application.h"
#include "core/Scene.h"
#include "core/Window.h"
#include "core/File.h"
#include "core/Components.h"
#include "gameplay/PlayerController.h"
#include "gameplay/CharacterController.h"
#include "world/ChunkManager.h"
#include "network/Network.h"

namespace Minecraft
{
	struct CommandStringView
	{
		const char* string;
		int length;
	};

	enum class CommandLineType : uint8
	{
		None,
		Give,
		Screenshot,
		GenerateCubemap,
		DoDaylightCycle,
		SetTime,
		StopNetwork,
		ReloadShaders,
		RegenerateWorld,
		BeginRecording,
		StopRecording,
		PlayRecording,
		Length
	};

	namespace CommandLine
	{
		bool isActive = false;

		static void parseCommand(const char* command, int length);
		static void executeCommand(CommandLineType type, CommandStringView* args, int argsLength);
		static void executeGivePlayer(CommandStringView* args, int argsLength);
		static void executeDoDaylightCycle(CommandStringView* args, int argsLength);
		static void executeSetTime(CommandStringView* args, int argsLength);

		static inline bool isNumber(char c) { return c >= '0' && c <= '9'; }
		static inline bool isIntegerDigit(char c) { return isNumber(c) || c == '+' || c == '-'; }
		static inline bool isFloatingPointDigit(char c) { return isIntegerDigit(c) || c == '.' || c == 'e' || c == 'E'; }
		static inline bool isCharIgnoreCase(char letter, char isThisChar) { return (tolower(letter) == tolower(isThisChar)); }
		static bool parseBoolean(const char* str, int strLength, bool* result);
		static bool isInteger(const char* str, int strLength);
		static Ecs::EntityId parsePlayer(const char* str, int strLength);

		void update(bool parseText)
		{
			Font* font = Fonts::getFont("assets/fonts/Minecraft.ttf", 16_px);
			if (font)
			{
				Style transparentSquare = Styles::defaultStyle;
				transparentSquare.color = "#00000088"_hex;
				glm::vec2 windowPos = glm::vec2(-2.95f, -1.35f);
				glm::vec2 windowSize = glm::vec2(5.9f, 0.1f);

				Gui::beginWindow(windowPos, windowSize);

				static std::array<char, 512> buffer = { '\0' };
				if (!parseText)
				{
					bool isFocused = true;
					Gui::input("", 0.0015f, buffer.data(), (int)buffer.size(), &isFocused, false, 4);
				}
				else
				{
					if (buffer[0] == '/')
					{
						parseCommand(&buffer[1], (int)buffer.size() - 1);
					}
					else
					{
						// TODO: Display the message to chat
						Ecs::EntityId localPlayer = World::getLocalPlayer();
						size_t chatMessageLength = strlen(buffer.data()) + 1;
						size_t sizeOfCommand = sizeof(Ecs::EntityId) + (sizeof(char) * chatMessageLength);
						SizedMemory memory = SizedMemory{ (uint8*)g_memory_allocate(sizeOfCommand), sizeOfCommand };
						uint8* data = memory.memory;
						std::strcpy((char*)data, &buffer[0]);
						data[chatMessageLength - 1] = '\0';
						uint8* entityIdDst = data + (chatMessageLength * sizeof(char));
						*(Ecs::EntityId*)entityIdDst = localPlayer;
						Network::sendClientCommand(ClientCommandType::Chat, memory);
						g_memory_free(memory.memory);

						MainHud::generalMessage(localPlayer, &buffer[0]);
					}

					g_memory_zeroMem(buffer.data(), 512 * sizeof(char));
				}

				Gui::endWindow();
			}
		}

		static void parseCommand(const char* command, int length)
		{
			if (command[0] == '\0')
			{
				return;
			}

			// Figure out which command (if any) we are trying to call
			CommandLineType type = CommandLineType::None;
			for (int i = 0; i < (int)CommandLineType::Length; i++)
			{
				auto enumName = magic_enum::enum_name((CommandLineType)i);
				if (isCharIgnoreCase(enumName[0], command[0]))
				{
					bool match = true;
					int commandTypeLength = 1;
					int minLength = glm::min((int)enumName.length() + 1, length);
					for (int strIndex = 1; strIndex < minLength; strIndex++)
					{
						if (command[strIndex] == '\0' || command[strIndex] == ' ')
						{
							type = (CommandLineType)i;
							commandTypeLength = strIndex;
							break;
						}

						if (strIndex < enumName.length() && !isCharIgnoreCase(enumName[strIndex], command[strIndex]))
						{
							match = false;
							break;
						}
					}

					if (match && commandTypeLength < length - 1)
					{
						// TODO: Only support for 10 arguments right now
						static CommandStringView args[10];
						int argsLength = 0;
						CommandStringView arg;

						// Find the start of the first argument by skipping whitespace
						int startArgIndex = commandTypeLength + 1;
						for (int i = startArgIndex; i < length; i++)
						{
							if ((command[i] != ' ' && command[i] != '\t') || command[i] == '\0')
							{
								arg.string = &command[i];
								startArgIndex = i;
								break;
							}
						}

						for (int i = startArgIndex; i < length; i++)
						{
							if (command[i] == ' ' || command[i] == '\0' || command[i] == '\t')
							{
								arg.length = (int)(&command[i] - arg.string);
								args[argsLength++] = arg;
								if (i < length - 1)
								{
									// Skip whitespace
									for (int j = i + 1; j < length; j++)
									{
										if ((command[j] != ' ' && command[j] != '\t') || command[j] == '\0')
										{
											arg.string = &command[j];
											i = j - 1;
											break;
										}
									}
								}

								if (command[i] == '\0' || arg.string[0] == '\0')
								{
									break;
								}
							}
						}

						executeCommand(type, &args[0], argsLength);
					}
				}
			}
		}

		static void executeCommand(CommandLineType type, CommandStringView* args, int argsLength)
		{
			switch (type)
			{
			case CommandLineType::Give:
				executeGivePlayer(args, argsLength);
				break;
			case CommandLineType::Screenshot:
				Application::takeScreenshot();
				break;
			case CommandLineType::GenerateCubemap:
				Application::getWindow().setSize(2160, 2160);
				PlayerController::generateCubemap = true;
				break;
			case CommandLineType::DoDaylightCycle:
				executeDoDaylightCycle(args, argsLength);
				break;
			case CommandLineType::SetTime:
				executeSetTime(args, argsLength);
				break;
			case CommandLineType::StopNetwork:
				Network::free();
				break;
			case CommandLineType::ReloadShaders:
				Scene::reloadShaders();
				break;
			case CommandLineType::RegenerateWorld:
				World::regenerateWorld();
				break;
			case CommandLineType::BeginRecording:
			{
				Scene::playFromEventFile = false;
				Scene::serializeEvents = true;
				std::string demoDir = World::getWorldReplayDirPath(World::savePath);
				// ----- Serialize initial world state
				World::pushSavePath(demoDir);
				World::serialize();
				ChunkManager::serializeSynchronous();
				World::popSavePath();

				Scene::queueMainEvent(GEventType::SetDeltaTime, &Application::deltaTime, sizeof(float), false);
				Scene::queueMainEventMoustInitial(Input::mouseX, Input::mouseY, Input::lastMouseX, Input::lastMouseY);
				Scene::queueMainEvent(GEventType::FrameTick);
				// -----
				g_logger_info("Recording demo to '%s'", (demoDir + "/replay.bin").c_str());
			}
			break;
			case CommandLineType::StopRecording:
			{
				Scene::serializeEvents = false;
				std::string demoDir = World::getWorldReplayDirPath(World::savePath);
				g_logger_info("Saved recorded demo at '%s'", (demoDir + "/replay.bin").c_str());
			}
			break;
			case CommandLineType::PlayRecording:
			{
				// Load the replay state
				std::string demoDir = World::getWorldReplayDirPath(World::savePath);
				Scene::changeScene(SceneType::Replay);
				g_logger_info("Playing demo at '%s'", demoDir.c_str());
			}
			break;
			default:
				g_logger_warning("Unknown command line type: %s", magic_enum::enum_name(type).data());
				break;
			}
		}

		static void executeGivePlayer(CommandStringView* args, int argsLength)
		{
			if (argsLength != 2 && argsLength != 3)
			{
				g_logger_warning("give expects 2 or 3 arguments. Syntax is '/give @[playerUsername] [blockName] <blockCount>'");
				return;
			}

			Ecs::EntityId player = parsePlayer(args[0].string, args[0].length);
			if (player == Ecs::nullEntity)
			{
				g_logger_warning("Cannot give block to player '%s' because that player does not exist.", args[1].string);
				return;
			}

			const std::string blockName = std::string(args[1].string, args[1].string + args[1].length);
			int blockId = BlockMap::getBlockId(blockName);

			int blockCount = 1;
			if (argsLength == 3)
			{
				if (!isInteger(args[2].string, args[2].length))
				{
					g_logger_warning("give expects an integer as the third argument, the block count.");
					return;
				}
				blockCount = atoi(args[2].string);
			}

			if (blockId)
			{
				if (!Network::isNetworkEnabled())
				{
					World::givePlayerBlock(player, blockId, blockCount);
				}
				else
				{
					if (Network::isLanServer())
					{
						World::givePlayerBlock(player, blockId, blockCount);
					}
					SizedMemory data = pack<int, int, Ecs::EntityId>(blockId, blockCount, player);
					Network::sendClientCommand(ClientCommandType::Give, data);
					g_memory_free(data.memory);
				}
			}
			else
			{
				g_logger_warning("Invalid block name '%s' in command SetInventorySlot", blockName.c_str());
			}
		}

		static void executeDoDaylightCycle(CommandStringView* args, int argsLength)
		{
			if (argsLength != 1)
			{
				g_logger_warning("DoDaylightCycle expects 1 argument: 'true' or 'false'.");
				return;
			}

			bool val;
			if (!parseBoolean(args[0].string, args[0].length, &val))
			{
				g_logger_warning("DoDaylightCycle expects 'true' or 'false'.");
				return;
			}

			World::doDaylightCycle = val;
			// TODO: Put this in the chat
			g_logger_info("DoDaylightCycle: %d", val);
		}

		static void executeSetTime(CommandStringView* args, int argsLength)
		{
			if (argsLength != 1)
			{
				g_logger_warning("SetTime expects 1 arguments: A number from 0-2400 representing the time.");
				return;
			}

			if (!isInteger(args[0].string, args[0].length))
			{
				g_logger_warning("SetTime expects an integer between 0-2400.");
				return;
			}
			int time = atoi(args[0].string);
			if (time < 0 || time > 2400)
			{
				g_logger_warning("Invalid time '%d' passed to SetTime. SetTime expects an integer between 0-2400.", time);
				return;
			}

			World::worldTime = time;
			SizedMemory timeData = pack<int>(time);
			Network::sendClientCommand(ClientCommandType::SetTime, timeData);
			g_memory_free(timeData.memory);
		}

		static bool parseBoolean(const char* str, int strLength, bool* result)
		{
			// The words True or False are at least 4 characters long
			if (strLength < 4)
			{
				return false;
			}

			// It could be the word 'false'
			if (isCharIgnoreCase(str[0], 'f'))
			{
				if (strLength != 5)
				{
					return false;
				}


				if (isCharIgnoreCase(str[1], 'a') && isCharIgnoreCase(str[2], 'l') && isCharIgnoreCase(str[3], 's') &&
					isCharIgnoreCase(str[4], 'e'))
				{
					*result = false;
					return true;
				}
			}
			// It could be the word 'true'
			else if (isCharIgnoreCase(str[0], 't'))
			{
				if (strLength != 4)
				{
					return false;
				}

				if (isCharIgnoreCase(str[1], 'r') && isCharIgnoreCase(str[2], 'u') && isCharIgnoreCase(str[3], 'e'))
				{
					*result = true;
					return true;
				}
			}

			return false;
		}

		static Ecs::EntityId parsePlayer(const char* str, int strLength)
		{
			if (strLength <= 0)
			{
				return Ecs::nullEntity;
			}

			if (str[0] != '@')
			{
				return Ecs::nullEntity;
			}

			// First check if the user type @me
			if (strLength >= 3)
			{
				if (str[0] == '@' && isCharIgnoreCase(str[1], 'm'), isCharIgnoreCase(str[2], 'e'))
				{
					return World::getLocalPlayer();
				}
			}

			// Otherwise, check if they typed any of the player usernames available
			Ecs::Registry* registry = Scene::getRegistry();
			for (auto entity : registry->view<PlayerComponent>())
			{
				const PlayerComponent& playerComponent = registry->getComponent<PlayerComponent>(entity);
				int playerNameLength = (int)std::strlen(playerComponent.name);
				bool isPlayer = true;
				for (int i = 0; i < playerNameLength; i++)
				{
					// If we're out of range of our string identifier, or we don't have the same character
					// This is not the correct string
					if ((i + 1) >= strLength || !isCharIgnoreCase(str[i + 1], playerComponent.name[i]))
					{
						isPlayer = false;
						break;
					}
				}

				if (isPlayer)
				{
					return entity;
				}
			}

			return Ecs::nullEntity;
		}

		static bool isInteger(const char* str, int strLength)
		{
			for (int i = 0; i < strLength; i++)
			{
				if (!isIntegerDigit(str[i]))
				{
					return false;
				}
			}

			return true;
		}
	}
}
