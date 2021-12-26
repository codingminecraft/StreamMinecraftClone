#ifndef MINECRAFT_PLAYER_CONTROLLER_H
#define MINECRAFT_PLAYER_CONTROLLER_H
#include "core/Ecs.h"

namespace Minecraft
{
	constexpr int MAX_NAME_LENGTH = 32;
	struct PlayerComponent
	{
		char name[MAX_NAME_LENGTH];

		inline void setName(const char* inName)
		{
			size_t length = std::strlen(inName) + 1;
			if (length > MAX_NAME_LENGTH)
			{
				g_logger_error("PlayerComponent only accepts names up to 32 characters. '%s' is too long.", inName);
				length = MAX_NAME_LENGTH;
			}

			g_memory_copyMem(name, (void*)inName, length * sizeof(char));
			name[length - 1] = '\0';
		}
	};

	namespace PlayerController
	{
		// TODO: Find a better way to do this
		extern bool generateCubemap;

		void init();

		void update(Ecs::Registry& registry, float dt);

		void setPlayerIfNeeded(bool forceOverride = false);
	};
}

#endif