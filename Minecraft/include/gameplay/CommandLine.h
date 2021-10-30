#ifndef MINECRAFT_COMMAND_LINE_H
#define MINECRAFT_COMMAND_LINE_H
#include "core.h"

namespace Minecraft
{
	namespace CommandLine
	{
		extern bool isActive;

		void update(float dt, bool parseText);
	}
}

#endif