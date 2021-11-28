#include "core/Ecs.h"
#include "core/Components.h"

namespace Minecraft
{
	namespace Ecs
	{
		static void write(RawMemory& memory, uint8* data, size_t dataSize, size_t* offset);
		static void read(RawMemory& memory, uint8* data, size_t dataSize, size_t* offset);

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
			memory.size = entityIdDataSize;
			memory.data = (uint8*)(g_memory_allocate(memory.size));
			size_t offset = 0;
			uint32 numEntities = (uint32)entities.size();
			write(memory, (uint8*)(&numEntities), sizeof(uint32), &offset);
			for (Ecs::EntityId entity : entities)
			{
				write(memory, (uint8*)&entity, sizeof(EntityId), &offset);
				int numComponents = this->numComponents(entity);
				write(memory, (uint8*)&numComponents, sizeof(int32), &offset);
				for (int i = 0; i < componentSets.size(); i++)
				{
					if (hasComponentById(entity, i))
					{
						write(memory, (uint8*)&i, sizeof(int32), &offset);
						size_t componentSize = componentSets[i].componentSize;
						uint8* componentData = componentSets[i].get(Internal::getEntityIndex(entity));
						write(memory, componentData, componentSize, &offset);
					}
				}
			}

			memory.data = (uint8*)g_memory_realloc(memory.data, offset);
			memory.size = offset;
			return memory;
		}

		void Ecs::Registry::deserialize(RawMemory memory)
		{
			size_t offset = 0;
			uint32 numEntities;
			read(memory, (uint8*)&numEntities, sizeof(uint32), &offset);
			entities.resize(numEntities);
			for (int entityCounter = 0; entityCounter < numEntities; entityCounter++)
			{
				EntityId entity;
				read(memory, (uint8*)&entity, sizeof(EntityId), &offset);
				entities[Internal::getEntityIndex(entity)] = entity;
				int numComponents;
				read(memory, (uint8*)&numComponents, sizeof(int), &offset);
				g_logger_assert(numComponents >= 0, "Deserialized bad data.");
				for (int componentCounter = 0; componentCounter < numComponents; componentCounter++)
				{
					int32 componentId;
					read(memory, (uint8*)&componentId, sizeof(int32), &offset);
					uint8* componentData = addOrGetComponentById(entity, componentId);
					size_t componentSize = componentSets[componentId].componentSize;
					read(memory, componentData, componentSize, &offset);
				}
			}
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

		static void write(RawMemory& memory, uint8* data, size_t dataSize, size_t* offset)
		{
			if (*offset + dataSize >= memory.size)
			{
				// Reallocate
				size_t newSize = memory.size * 2;
				uint8* newData = (uint8*)g_memory_realloc(memory.data, newSize);
				g_logger_assert(newData != nullptr, "Failed to Reallocate more memory.");
				memory.data = newData;
				memory.size = newSize;
			}

			g_memory_copyMem(memory.data + *offset, data, dataSize);
			*offset += dataSize;
		}

		static void read(RawMemory& memory, uint8* data, size_t dataSize, size_t* offset)
		{
			if (*offset + dataSize > memory.size)
			{
				g_logger_error("Bad data.");
				return;
			}

			g_memory_copyMem(data, memory.data + *offset, dataSize);
			*offset += dataSize;
		}
	}
}
