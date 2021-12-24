#include "core/Ecs.h"
#include "core/Components.h"

namespace Minecraft
{
	namespace Ecs
	{
		namespace Internal
		{
			int32 ComponentCounter = 0;
		}

		EntityId nullEntity = Internal::createEntityId(UINT32_MAX, 0);

		void Internal::SparseSet::free()
		{
			if (pools != nullptr)
			{
				g_memory_free(pools);
				pools = nullptr;
			}

			if (entities != nullptr)
			{
				g_memory_free(entities);
				entities = nullptr;
			}

			if (data != nullptr)
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

		RawMemory Ecs::Registry::serialize()
		{
			// uint32 numEntities
			// Begin Loooping entities
			// uint64 entityId
			// uint16 numComponents
			// Begin Looping components
			// int32 componentId
			// Copy component into this entity id or create this entity if it does not exist
			RawMemory memory;
			size_t entityIdDataSize = sizeof(uint32) + (sizeof(uint16) * entities.size());
			memory.init(entityIdDataSize);

			uint32 numEntities = (uint32)entities.size();
			memory.write<uint32>(&numEntities);
			for (Ecs::EntityId entity : entities)
			{
				memory.write<EntityId>(&entity);
				int32 numComponents = this->numComponents(entity);
				memory.write<int32>(&numComponents);
				for (int i = 0; i < componentSets.size(); i++)
				{
					if (hasComponentById(entity, i))
					{
						memory.write<int32>(&i);
						size_t componentSize = componentSets[i].componentSize;
						uint8* componentData = componentSets[i].get(Internal::getEntityIndex(entity));
						memory.writeDangerous(componentData, componentSize);
					}
				}
			}

			memory.shrinkToFit();
			return memory;
		}

		void Ecs::Registry::deserialize(RawMemory& memory)
		{
			memory.resetReadWriteCursor();

			uint32 numEntities;
			memory.read<uint32>(&numEntities);
			entities.resize(numEntities);
			for (uint32 entityCounter = 0; entityCounter < numEntities; entityCounter++)
			{
				EntityId entity;
				memory.read<EntityId>(&entity);
				entities[Internal::getEntityIndex(entity)] = entity;
				int32 numComponents;
				memory.read<int32>(&numComponents);
				g_logger_assert(numComponents >= 0, "Deserialized bad data.");
				for (int componentCounter = 0; componentCounter < numComponents; componentCounter++)
				{
					int32 componentId;
					memory.read<int32>(&componentId);
					uint8* componentData = addOrGetComponentById(entity, componentId);
					size_t componentSize = componentSets[componentId].componentSize;
					memory.readDangerous(componentData, componentSize);
				}
			}
			g_logger_info("Deserialized %d entities.", numEntities);
		}

		int Ecs::Registry::numComponents(EntityId entity) const
		{
			int numComponents = 0;
			for (int componentId = 0; componentId < componentSets.size(); componentId++)
			{
				if (hasComponentById(entity, componentId))
				{
					numComponents++;
				}
			}

			return numComponents;
		}

		EntityId Registry::find(TagType type)
		{
			auto tagView = view<Tag>();
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
