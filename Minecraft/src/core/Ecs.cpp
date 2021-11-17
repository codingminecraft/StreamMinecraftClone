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

		EntityId Registry::find(TagType type)
		{
			auto& tagView = view<Tag>();
			for (EntityId entity : tagView)
			{
				if (getComponent<Tag>(entity).type == type)
				{
					return entity;
				}
			}

			return nullEntity;
		}
	}
}
