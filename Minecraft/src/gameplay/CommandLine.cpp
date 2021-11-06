#include "gameplay/CommandLine.h"
#include "renderer/Font.h"
#include "renderer/Styles.h"
#include "renderer/Renderer.h"
#include "gui/Gui.h"
#include "gui/MainHud.h"
#include "input/Input.h"
#include "world/BlockMap.h"
#include "core/Application.h"
#include "core/Window.h"
#include "gameplay/PlayerController.h"
#include "world/ChunkManager.h"

namespace Minecraft
{
	struct CommandStringView
	{
		const char* string;
		int length;
	};

	enum CommandLineType : uint8
	{
		None,
		SetInventorySlot,
		Screenshot,
		GenerateCubemap,
		DebugLight,
		Length
	};

	namespace CommandLine
	{
		extern bool isActive = false;

		static void parseCommand(const char* command, int length);
		static void executeCommand(CommandLineType type, CommandStringView* args, int argsLength);
		static void executeSetInventorySlot(CommandStringView* args, int argsLength);
		static void executeDebugLight(CommandStringView* args, int argsLength);

		static inline bool isNumber(char c) { return c >= '0' && c <= '9'; }
		static inline bool isIntegerDigit(char c) { return isNumber(c) || c == '+' || c == '-'; }
		static inline bool isFloatingPointDigit(char c) { return isIntegerDigit(c) || c == '.' || c == 'e' || c == 'E'; }
		static inline bool isCharIgnoreCase(char letter, char isThisChar) { return (tolower(letter) == tolower(isThisChar)); }
		static bool parseBoolean(const char* str, int strLength, bool* result);
		static bool isInteger(const char* str, int strLength);

		void update(float dt, bool parseText)
		{
			Font* font = Fonts::getFont("assets/fonts/Minecraft.ttf", 16_px);
			if (font)
			{
				Style transparentSquare = Styles::defaultStyle;
				transparentSquare.color = "#00000088"_hex;
				glm::vec2 windowPos = glm::vec2(-2.95f, -1.35f);
				glm::vec2 windowSize = glm::vec2(5.9f, 0.1f);

				Gui::beginWindow(windowPos, windowSize);

				static std::array<char, 512> buffer;
				if (!parseText)
				{
					Gui::input("", 0.0015f, buffer.data(), buffer.size(), false, true, 3);
				}
				else
				{
					if (buffer[0] == '/')
					{
						parseCommand(&buffer[1], buffer.size() - 1);
					}
					else
					{
						// TODO: Display the message to chat
					}

					buffer[0] = '\0';
				}

				Gui::endWindow();
			}
		}

		static void parseCommand(const char* command, int length)
		{
			// Figure out which command (if any) we are trying to call
			CommandLineType type = CommandLineType::None;
			for (int i = 0; i < (int)CommandLineType::Length; i++)
			{
				auto enumName = magic_enum::enum_name((CommandLineType)i);
				if (enumName[0] == command[0])
				{
					bool match = true;
					int commandTypeLength = 1;
					for (int strIndex = 1; strIndex < length; strIndex++)
					{
						if (command[strIndex] == '\0' || command[strIndex] == ' ')
						{
							type = (CommandLineType)i;
							commandTypeLength = strIndex;
							break;
						}

						if (strIndex < enumName.length() && enumName[strIndex] != command[strIndex])
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
						arg.string = &command[commandTypeLength + 1];
						for (int i = commandTypeLength + 1; i < length; i++)
						{
							if (command[i] == ' ' || command[i] == '\0')
							{
								arg.length = &command[i] - arg.string;
								args[argsLength++] = arg;
								if (i < length - 1)
								{
									arg.string = &command[i + 1];
								}

								if (command[i] == '\0')
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
			case CommandLineType::SetInventorySlot:
				executeSetInventorySlot(args, argsLength);
				break;
			case CommandLineType::Screenshot:
				Application::takeScreenshot();
				break;
			case CommandLineType::GenerateCubemap:
				Application::getWindow().setSize(1280.0f, 1280.0f);
				PlayerController::generateCubemap = true;
				break;
			case CommandLineType::DebugLight:
				executeDebugLight(args, argsLength);
				break;
			default:
				g_logger_warning("Unknown command line type: %s", magic_enum::enum_name(type).data());
				break;
			}
		}

		static void executeSetInventorySlot(CommandStringView* args, int argsLength)
		{
			if (argsLength != 2)
			{
				g_logger_warning("SetInventorySlot expects 2 arguments.");
				return;
			}

			if (!isInteger(args[0].string, args[0].length))
			{
				g_logger_warning("SetInventorySlot expects an integer for the first argument.");
				return;
			}
			int slot = atoi(args[0].string) - 1;
			if (slot < 0 || slot > 9)
			{
				g_logger_warning("Invalid inventory slot '%d' passed to SetInventorySlot.", slot);
				return;
			}

			const std::string blockName = std::string(args[1].string, args[1].string + args[1].length);
			int blockId = BlockMap::getBlockId(blockName);
			if (blockId)
			{
				MainHud::hotbarBlockIds[slot] = blockId;
			}
			else
			{
				g_logger_warning("Invalid block name '%s' in command SetInventorySlot", blockName.c_str());
			}
		}

		static void executeDebugLight(CommandStringView* args, int argsLength)
		{
			if (argsLength != 1)
			{
				g_logger_warning("DebugLight expects 1 argument.");
				return;
			}

			bool val;
			if (!parseBoolean(args[0].string, args[0].length, &val))
			{
				g_logger_warning("DebugLight expects 'true' or 'false'.");
				return;
			}

			ChunkManager::doStepLogic = val;
			if (val)
			{
				g_logger_info("Debug Lighting turned on. Run command DebugStepLight to step the lighting calculations by one. Run DebugLight false to turn debugging off.");
			}
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
