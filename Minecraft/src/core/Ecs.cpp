#include "core/Ecs.h"

namespace Minecraft
{
	namespace Ecs
	{
		namespace Internal
		{
			extern int32 ComponentCounter = 0;
		}

		extern EntityId nullEntity = Internal::createEntityId(UINT32_MAX, 0);

		void ComponentPool::free()
		{
			if (data)
			{
				g_memory_free(data);
			}
		}
	}
}
