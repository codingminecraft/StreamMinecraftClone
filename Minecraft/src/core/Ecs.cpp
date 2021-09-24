#include "core/Ecs.h"
#include "core/Components.h"

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

		EntityId Registry::find(TagType type)
		{
			for (EntityDescription& description : entities)
			{
				if (hasComponent<Tag>(description.id) && getComponent<Tag>(description.id).type == type)
				{
					return description.id;
				}
			}

			return nullEntity;
		}
	}
}
