#include "network/Network.h"
#include "core.h"

#include <enet/enet.h>

namespace Minecraft
{
	namespace Network
	{
		void init()
		{
			if (enet_initialize() != 0)
			{
				g_logger_assert(false,
					"An error occurred while initializing ENet.\nTODO: This doesn't effect single-player mode, "
					"add in a gameplay mode that doesn't require ENet.");
			}
		}

		void free()
		{
			enet_deinitialize();
		}
	}
}
