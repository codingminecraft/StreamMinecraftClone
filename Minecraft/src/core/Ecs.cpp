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

		void Internal::SparseSet::free()
		{
			if (pools)
			{
				g_memory_free(pools);
				pools = nullptr;
			}

			if (entities)
			{
				g_memory_free(entities);
				entities = nullptr;
			}

			if (data)
			{
				g_memory_free(data);
				data = nullptr;
			}

			maxNumComponents = 0;
			numComponents = 0;
			numPools = 0;
		}

		void Registry::free()
		{
			for (Internal::SparseSet& set : componentSets)
			{
				set.free();
			}
		}

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
