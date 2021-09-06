#ifndef MINECRAFT_ECS_H
#define MINECRAFT_ECS_H
#include "core.h"

namespace Minecraft
{
	namespace Ecs
	{
		template<typename... Components>
		class RegistryView;

		typedef uint32 EntityIndex;
		typedef uint32 EntityVersion;
		typedef uint64 EntityId;
		extern EntityId nullEntity;

		namespace Internal
		{
			extern int32 ComponentCounter;
			const int MAX_COMPONENTS = 32;
			const int MAX_ENTITIES = 4096;

			inline EntityId createEntityId(EntityIndex index, EntityVersion version)
			{
				return  ((EntityId)version << 32) | ((EntityId)index);
			}

			inline EntityIndex getEntityIndex(EntityId id)
			{
				return (EntityIndex)id;
			}

			inline EntityVersion getEntityVersion(EntityId id)
			{
				return (EntityVersion)id >> 32;
			}

			inline bool isEntityValid(EntityId id)
			{
				return getEntityIndex(id) != getEntityIndex(nullEntity);
			}
		}

		typedef std::bitset<Internal::MAX_COMPONENTS> ComponentMask;

		template<typename T>
		int32 componentId()
		{
			static int32 componentId = Internal::ComponentCounter++;
			return componentId;
		}

		inline bool isNull(EntityId entity)
		{
			return Internal::getEntityIndex(entity) == Internal::getEntityIndex(nullEntity);
		}

		struct ComponentPool
		{
			char* data;
			size_t componentSize;

			void free();

			template<typename T>
			inline T* get(int index)
			{
				return (T*)(data + index * componentSize);
			}

			template<typename T>
			void init()
			{
				g_logger_assert(std::is_pod<T>(), "Component must be POD. Component %s is not POD.", typeid(T).name());
				componentSize = sizeof(T);
				data = (char*)g_memory_allocate(componentSize * Internal::MAX_ENTITIES);
			}

			template<typename T>
			static ComponentPool defaultPool()
			{
				ComponentPool res;
				res.data = nullptr;
				res.componentSize = 0;
				return res;
			}
		};

		struct Registry
		{
			struct EntityDescription
			{
				EntityId id;
				ComponentMask mask;
			};
			std::vector<EntityDescription> entities;
			std::vector<ComponentPool> componentPools;
			std::vector<EntityIndex> freeEntities;

			EntityId createEntity()
			{
				if (!freeEntities.empty())
				{
					EntityIndex newIndex = freeEntities.back();
					freeEntities.pop_back();
					EntityId newId = Internal::createEntityId(newIndex, Internal::getEntityVersion(entities[newIndex].id));
					entities[newIndex].id = newId;
					return entities[newIndex].id;
				}
				entities.emplace_back(EntityDescription{ Internal::createEntityId((uint32)entities.size(), 0), ComponentMask() });
				return entities.back().id;
			}

			void free()
			{
				for (ComponentPool pool : componentPools)
				{
					pool.free();
				}
			}

			template<typename T>
			T& addComponent(EntityId id)
			{
				int32 componentId = Ecs::componentId<T>();

				if (componentPools.size() <= componentId)
				{
					componentPools.resize(componentId + 1, ComponentPool::defaultPool<T>());
				}
				if (!componentPools[componentId].data)
				{
					componentPools[componentId].init<T>();
				}

				// Get the component at this id and initialize it to 0
				T& component = *componentPools[componentId].get<T>(id);
				g_memory_zeroMem(&component, sizeof(T));

				// Set the bitmask to indicate this entity has this component
				entities[Internal::getEntityIndex(id)].mask.set(componentId, true);
				return component;
			}

			bool validEntity(EntityId id)
			{
				return Internal::getEntityIndex(id) < entities.size() && !isNull(id);
			}

			template<typename T>
			bool hasComponent(EntityId id)
			{
				g_logger_assert(validEntity(id), "Cannot check if invalid entity %d has a component.", Internal::getEntityIndex(id));
				int32 componentId = Ecs::componentId<T>();
				return entities[Internal::getEntityIndex(id)].mask.test(componentId);
			}

			template<typename T>
			T& getComponent(EntityId id)
			{
				g_logger_assert(hasComponent<T>(id), "Entity %d does not have component", Internal::getEntityIndex(id));
				int32 componentId = Ecs::componentId<T>();
				return *componentPools[componentId].get<T>(id);
			}

			template<typename T>
			void remove(EntityId id)
			{
				g_logger_assert(validEntity(id), "Cannot remove invalid entity %d.", Internal::getEntityIndex(id));
				int32 componentId = Ecs::componentId<T>();
				entities[Internal::getEntityIndex(id)].mask.set(componentId, false);
			}

			template<typename... Components>
			RegistryView<Components...> view()
			{
				return RegistryView<Components...>(*this);
			}

			void destroyEntity(EntityId id)
			{
				EntityId newId = Internal::createEntityId(UINT32_MAX, Internal::getEntityVersion(id) + 1);
				entities[Internal::getEntityIndex(id)].id = newId;
				entities[Internal::getEntityIndex(id)].mask.reset();
				freeEntities.push_back(Internal::getEntityIndex(id));
			}
		};

		template<typename... Components>
		class RegistryView
		{
		public:
			RegistryView(Registry& registry)
				: registry(registry)
			{
				all = sizeof...(Components) == 0;
				if (!all)
				{
					int componentIds[] = { 0, Ecs::componentId<Components>() ... };
					for (int i = 1; i < (sizeof...(Components) + 1); i++)
					{
						componentMask.set(componentIds[i]);
					}
				}
			}

			class Iterator
			{
			public:
				Iterator(Registry& registry, EntityIndex index, ComponentMask mask, bool all) 
					: registry(registry), index(index), mask(mask), all(all)
				{
				}

				EntityId operator*() const
				{
					return registry.entities[index].id;
				}

				bool operator==(const Iterator& other) const
				{
					return index == other.index || index == registry.entities.size();
				}

				bool operator!=(const Iterator& other) const
				{
					return !(*this == other);
				}

				Iterator& operator++()
				{
					do
					{
						index++;
					} while (index < registry.entities.size() && !validIndex());
					return *this;
				}

			private:
				bool validIndex()
				{
					// Is it a valid entity and has the correct component bitmask
					return registry.validEntity(registry.entities[index].id) &&
						(all || mask == (mask & registry.entities[index].mask));
				}

			private:
				EntityIndex index;
				Registry& registry;
				ComponentMask mask;
				bool all;
			};

			const Iterator begin() const
			{
				int firstIndex = 0;
				while (firstIndex < registry.entities.size() &&
					(componentMask != (componentMask & registry.entities[firstIndex].mask)
						|| !registry.validEntity(registry.entities[firstIndex].id)))
				{
					firstIndex++;
				}
				return Iterator(registry, firstIndex, componentMask, all);
			}

			const Iterator end() const
			{
				return Iterator(registry, registry.entities.size(), componentMask, all);
			}

		private:
			Registry& registry;
			ComponentMask componentMask;
			bool all;
		};
	}
}

#endif