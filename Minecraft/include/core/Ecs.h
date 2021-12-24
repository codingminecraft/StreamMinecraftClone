#ifndef MINECRAFT_ECS_H
#define MINECRAFT_ECS_H
#include "core.h"

namespace Minecraft
{
	enum class TagType : uint8;

	namespace Ecs
	{
		template<typename... Components>
		class RegistryView;

		template<typename T>
		int32 componentId();

		typedef uint32 EntityIndex;
		typedef uint32 EntityVersion;
		typedef uint64 EntityId;
		extern EntityId nullEntity;

		namespace Internal
		{
			extern int32 ComponentCounter;
			const int sparseSetPoolSize = 8;
			const int32 MaxNumComponents = 256;

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
				return (EntityVersion)(id >> 32);
			}

			inline bool isEntityValid(EntityId id)
			{
				return getEntityIndex(id) != getEntityIndex(nullEntity);
			}

			struct SparseSetPool
			{
				EntityIndex startIndex;
				EntityIndex entities[sparseSetPoolSize];

				void init()
				{
					const EntityIndex nullIndex = getEntityIndex(nullEntity);
					for (int i = 0; i < sparseSetPoolSize; i++)
					{
						entities[i] = nullIndex;
					}
				}
			};

			struct SparseSet
			{
				int componentId;
				uint32 maxNumComponents;
				uint32 numComponents;
				int numPools;

				SparseSetPool* pools;
				// Number of entities is always equal to numComponents for a sparse set
				EntityIndex* entities;
				char* data;
				size_t componentSize;

				void free();

				inline int getPoolAlignedIndex(EntityIndex index)
				{
					// We need to make sure pools always start on a multiple of the pool size
					// to ensure no pools intersect
					return (index / sparseSetPoolSize) * sparseSetPoolSize;
				}

				inline SparseSetPool* getPool(EntityIndex index) const
				{
					// TODO: Change this to a hashmap or something for O(1) lookup time
					// For now do a simple linear search through the pools
					for (int i = 0; i < numPools; i++)
					{
						SparseSetPool& pool = pools[i];
						if (pool.startIndex <= index && pool.startIndex + sparseSetPoolSize > index)
						{
							return &pool;
						}
					}

					return nullptr;
				}

				template<typename T>
				inline T* get(EntityIndex index)
				{
					// TODO: Use the other get function
					SparseSetPool* pool = getPool(index);
					if (!pool)
					{
						g_logger_error("Invalid entity '%d' for component '%d'", index, componentId);
						return nullptr;
					}

					EntityIndex denseArrayIndex = pool->entities[index - pool->startIndex];
					g_logger_assert((denseArrayIndex < numComponents && denseArrayIndex >= 0), "Invalid dense array index.");
					return (T*)(data + denseArrayIndex * componentSize);
				}

				inline uint8* get(EntityIndex index)
				{
					SparseSetPool* pool = getPool(index);
					if (!pool)
					{
						g_logger_error("Invalid entity '%d' for component '%d'", index, componentId);
						return nullptr;
					}

					EntityIndex denseArrayIndex = pool->entities[index - pool->startIndex];
					g_logger_assert((denseArrayIndex < numComponents&& denseArrayIndex >= 0), "Invalid dense array index.");
					return (uint8*)(data + denseArrayIndex * componentSize);
				}

				template<typename T>
				inline void add(EntityId entity, const T& component)
				{
					EntityIndex index = getEntityIndex(entity);
					SparseSetPool* pool = getPool(index);
					if (!pool)
					{
						const int newNumPools = numPools + 1;
						SparseSetPool* newPools = (SparseSetPool*)g_memory_realloc(pools, newNumPools * sizeof(SparseSetPool));
						if (!newPools)
						{
							g_logger_error("Failed to allocate memory for new sparse set pool for component '%d'", componentId);
							return;
						}
						numPools = newNumPools;
						pools = newPools;
						pools[numPools - 1].init();
						pools[numPools - 1].startIndex = getPoolAlignedIndex(index);
						pool = &pools[numPools - 1];
					}

					uint32 nextIndex = numComponents;
					if (nextIndex >= maxNumComponents)
					{
						int newMaxNumComponents = maxNumComponents * 2;
						char* newComponentMemory = (char*)g_memory_realloc(data, componentSize * newMaxNumComponents);
						EntityIndex* newEntityMemory = (EntityIndex*)g_memory_realloc(entities, sizeof(EntityIndex) * newMaxNumComponents);
						if (!newComponentMemory || !newEntityMemory)
						{
							// Just free both of the reallocs if it ever fails
							g_memory_free(newComponentMemory);
							g_memory_free(newEntityMemory);
							g_logger_error("Failed to allocate new memory for component pool or entities for component '%d'", componentId);
							return;
						}
						data = newComponentMemory;
						entities = newEntityMemory;
						maxNumComponents = newMaxNumComponents;
					}

					pool->entities[index - pool->startIndex] = nextIndex;
					g_memory_copyMem(data + nextIndex * componentSize, (void*)&component, componentSize);
					entities[nextIndex] = index;
					numComponents++;
				}

				inline void add(EntityId entity)
				{
					EntityIndex index = getEntityIndex(entity);
					SparseSetPool* pool = getPool(index);
					if (!pool)
					{
						const int newNumPools = numPools + 1;
						SparseSetPool* newPools = (SparseSetPool*)g_memory_realloc(pools, newNumPools * sizeof(SparseSetPool));
						if (!newPools)
						{
							g_logger_error("Failed to allocate memory for new sparse set pool for component '%d'", componentId);
							return;
						}
						numPools = newNumPools;
						pools = newPools;
						pools[numPools - 1].init();
						pools[numPools - 1].startIndex = getPoolAlignedIndex(index);
						pool = &pools[numPools - 1];
					}

					uint32 nextIndex = numComponents;
					if (nextIndex >= maxNumComponents)
					{
						int newMaxNumComponents = maxNumComponents * 2;
						char* newComponentMemory = (char*)g_memory_realloc(data, componentSize * newMaxNumComponents);
						EntityIndex* newEntityMemory = (EntityIndex*)g_memory_realloc(entities, sizeof(EntityIndex) * newMaxNumComponents);
						if (!newComponentMemory || !newEntityMemory)
						{
							// Just free both of the reallocs if it ever fails
							g_memory_free(newComponentMemory);
							g_memory_free(newEntityMemory);
							g_logger_error("Failed to allocate new memory for component pool or entities for component '%d'", componentId);
							return;
						}
						data = newComponentMemory;
						entities = newEntityMemory;
						maxNumComponents = newMaxNumComponents;
					}

					pool->entities[index - pool->startIndex] = nextIndex;
					g_memory_zeroMem(data + nextIndex * componentSize, componentSize);
					entities[nextIndex] = index;
					numComponents++;
				}

				template<typename T>
				inline T* addOrGet(EntityId entity)
				{
					if (!exists(entity))
					{
						add<T>(entity, T{});
					}

					return get<T>(Internal::getEntityIndex(entity));
				}

				inline uint8* addOrGet(EntityId entity)
				{
					if (!exists(entity))
					{
						add(entity);
					}

					return get(Internal::getEntityIndex(entity));
				}

				inline bool exists(EntityId entity) const
				{
					const EntityIndex index = getEntityIndex(entity);
					SparseSetPool* pool = getPool(index);
					if (!pool)
					{
						return false;
					}

					return pool->entities[index - pool->startIndex] != getEntityIndex(nullEntity);
				}

				inline void remove(EntityId entity)
				{
					const EntityIndex index = getEntityIndex(entity);
					SparseSetPool* pool = getPool(index);
					if (!pool || index >= numComponents)
					{
						g_logger_warning("Tried to remove an entity '%d' that did not exist for component '%d'", index, componentId);
						return;
					}

					EntityIndex denseArrayIndex = Internal::getEntityIndex(pool->entities[index - pool->startIndex]);
					if (denseArrayIndex < numComponents - 1 && numComponents > 2)
					{
						// If the component data is not already at the end of the component array
						// Swap it with the component at the end of the array and update all indices accordingly
						EntityIndex entityToSwapIndex = entities[numComponents - 1];
						SparseSetPool* entityToSwapPool = pool;
						if (entityToSwapIndex < entityToSwapPool->startIndex || entityToSwapIndex >= entityToSwapPool->startIndex + sparseSetPoolSize)
						{
							entityToSwapPool = getPool(entityToSwapIndex);
							g_logger_assert(entityToSwapPool != nullptr, "Invalid entity was somehow stored in the dense array...");
						}

						// Swap this entity with the other entity
						entityToSwapPool->entities[entityToSwapIndex - entityToSwapPool->startIndex] = denseArrayIndex;
						entities[denseArrayIndex] = entities[numComponents - 1];
						// Swap the components
						data[denseArrayIndex] = data[numComponents - 1];
					}

					// Mark this entity as gone and decrease the numComponents
					pool->entities[index - pool->startIndex] = getEntityIndex(nullEntity);
					numComponents--;
				}

				template<typename T>
				void init(EntityIndex startIndex)
				{
					g_logger_assert(std::is_pod<T>(), "Component must be POD. Component %s is not POD.", typeid(T).name());
					componentSize = sizeof(T);

					numPools = 1;
					pools = (SparseSetPool*)g_memory_allocate(numPools * sizeof(SparseSetPool));
					pools[0].init();
					pools[0].startIndex = getPoolAlignedIndex(startIndex);

					numComponents = 0;
					maxNumComponents = Internal::sparseSetPoolSize;
					data = (char*)g_memory_allocate(componentSize * maxNumComponents);
					entities = (EntityIndex*)g_memory_allocate(sizeof(EntityIndex) * maxNumComponents);
				}

				template<typename T>
				static SparseSet defaultSet(EntityIndex index)
				{
					SparseSet res;
					res.componentId = Ecs::componentId<T>();
					res.init<T>(index);

					return res;
				}
			};
		}

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

		struct Registry
		{
			std::vector<EntityId> entities;
			std::vector<Internal::SparseSet> componentSets;
			std::vector<EntityIndex> freeEntities;
			std::vector<std::string> debugComponentNames;

			EntityId createEntity()
			{
				if (!freeEntities.empty())
				{
					EntityIndex newIndex = freeEntities.back();
					freeEntities.pop_back();
					// TODO: Versioning is broken, see if entity versioning is even necessary or not, it probably is...
					EntityId newId = Internal::createEntityId(newIndex, Internal::getEntityVersion(entities[newIndex]));
					entities[newIndex] = newId;
					return entities[newIndex];
				}
				entities.emplace_back(Internal::createEntityId((uint32)entities.size(), 0));
				return entities.back();
			}

			RawMemory serialize();

			void deserialize(RawMemory& memory);

			void free();

			int numComponents(EntityId entity) const;

			void clear()
			{
				entities.clear();
				for (Internal::SparseSet& set : componentSets)
				{
					set.free();
				}
				componentSets.clear();
				componentSets = std::vector<Internal::SparseSet>();
				debugComponentNames.clear();
				freeEntities.clear();
			}

			template<typename T>
			void registerComponent(const char* debugName)
			{
				int32 compId = Ecs::componentId<T>();

				// TODO: Maybe do something different here?
				EntityIndex index = 0;

				g_logger_assert(compId == componentSets.size(), "Tried to register component '%s' twice.", debugName);
				componentSets.emplace_back(Internal::SparseSet::defaultSet<T>(index));
				g_logger_assert(compId < Internal::MaxNumComponents, "Exceeded the maximum number of components, you can increase this if needed.");
				debugComponentNames.emplace_back(std::string(debugName));
			}

			template<typename T>
			T& addComponent(EntityId id)
			{
				int32 componentId = Ecs::componentId<T>();
				const EntityIndex index = Internal::getEntityIndex(id);

				g_logger_assert(componentId < componentSets.size(), "You need to register all components in the same order *everywhere*. Component '%s' was not registered.", typeid(T).name());

				// Get or add the component at this index
				T& component = *componentSets[componentId].addOrGet<T>(index);

				return component;
			}

			bool validEntity(EntityId id) const
			{
				return Internal::getEntityIndex(id) < entities.size() && !isNull(id);
			}

			template<typename T>
			bool hasComponent(EntityId id)
			{
				return hasComponentById(id, componentId<T>());
			}

			bool hasComponentById(EntityId id, int32 componentId) const
			{
				if (!validEntity(id))
				{
					g_logger_error("Cannot check if invalid entity %d has a component.", Internal::getEntityIndex(id));
					return false;
				}

				if (componentId >= componentSets.size() || componentId < 0)
				{
					g_logger_warning("Tried to check if an entity had component '%d', but a component of type '%d' does not exist in the registry which only has '%d' components.", componentId, componentId, componentSets.size());
					return false;
				}

				return componentSets[componentId].exists(id);
			}

			uint8* getComponentById(EntityId id, int32 componentId)
			{
				if (!validEntity(id))
				{
					g_logger_error("Cannot check if invalid entity %d has a component.", Internal::getEntityIndex(id));
					return nullptr;
				}

				if (componentId >= componentSets.size() || componentId < 0)
				{
					g_logger_warning("Tried to check if an entity had component '%d', but a component of type '%d' does not exist in the registry.", componentId, componentId);
					return nullptr;
				}

				return componentSets[componentId].get(Internal::getEntityIndex(id));
			}

			uint8* addOrGetComponentById(EntityId id, int32 componentId)
			{
				if (!validEntity(id))
				{
					g_logger_error("Cannot check if invalid entity %d has a component.", Internal::getEntityIndex(id));
					return nullptr;
				}

				if (componentId >= componentSets.size() || componentId < 0)
				{
					g_logger_warning("Tried to check if an entity had component '%d', but a component of type '%d' does not exist in the registry.", componentId, componentId);
					return nullptr;
				}

				return componentSets[componentId].addOrGet(id);
			}

			template<typename T>
			T& getComponent(EntityId id)
			{
				const EntityIndex index = Internal::getEntityIndex(id);
				int32 compId = Ecs::componentId<T>();
				g_logger_assert(hasComponent<T>(id), "Entity '%d' does not have component '%s'", id, debugComponentNames[compId].c_str());

				if (compId >= componentSets.size() || compId < 0)
				{
					g_logger_error("Tried to get invalid component '%d'", compId);
				}
				
				// TODO: This will crash if the component is null, should we return a null component or something?
				return *componentSets[compId].get<T>(index);
			}

			template<typename T>
			void removeComponent(EntityId id)
			{
				if (!validEntity(id))
				{
					g_logger_error("Tried to remove invalid entity %d.", Internal::getEntityIndex(id));
					return;
				}

				int32 compId = Ecs::componentId<T>();
				EntityIndex index = Internal::getEntityIndex(id);
				if (index >= entities.size())
				{
					g_logger_error("Tried to remove component from invalid entity '%d'", id);
					return;
				}

				if (compId < 0 || compId >= componentSets.size())
				{
					g_logger_error("Tried to remove component that does not exist '%d'", compId);
					return;
				}

				componentSets[compId].remove(id);
			}

			template<typename... Components>
			RegistryView<Components...> view()
			{
				return RegistryView<Components...>(*this);
			}

			void removeAllComponents(EntityId id)
			{
				EntityIndex index = Internal::getEntityIndex(id);
				if (index >= entities.size())
				{
					g_logger_error("Tried to remove all components from invalid entity '%d'", id);
					return;
				}

				for (int i = 0; i < componentSets.size(); i++)
				{
					if (componentSets[i].exists(id))
					{
						componentSets[i].remove(id);
					}
				}
			}

			void destroyEntity(EntityId id)
			{
				removeAllComponents(id);
				EntityId newId = Internal::createEntityId(UINT32_MAX, Internal::getEntityVersion(id) + 1);
				entities[Internal::getEntityIndex(id)] = newId;
				freeEntities.push_back(Internal::getEntityIndex(id));
			}

			EntityId find(TagType type);
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
				Iterator(Registry& registry, EntityIndex index, std::bitset<Internal::MaxNumComponents> mask, bool all)
					: registry(registry), index(index), mask(mask), all(all)
				{
				}

				EntityId operator*() const
				{
					return registry.entities[index];
				}

				bool operator==(const Iterator& other) const
				{
					return index == other.index;
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
					return index >= 0 && registry.validEntity(registry.entities[index]) &&
						(
							all || 
							RegistryView::hasRequiredComponents(registry, mask, registry.entities[index]
						));
				}

			private:
				EntityIndex index;
				Registry& registry;
				std::bitset<Internal::MaxNumComponents> mask;
				bool all;
			};

			const Iterator begin() const
			{
				int firstIndex = 0;
				while (firstIndex < registry.entities.size() &&
					(
						!hasRequiredComponents(registry, componentMask, registry.entities[firstIndex]) || 
						!registry.validEntity(registry.entities[firstIndex])
					))
				{
					firstIndex++;
				}
				return Iterator(registry, firstIndex, componentMask, all);
			}

			const Iterator end() const
			{
				return Iterator(registry, (EntityIndex)registry.entities.size(), componentMask, all);
			}

		private:
			static bool hasRequiredComponents(Registry& registry, const std::bitset<Internal::MaxNumComponents>& mask, EntityId entity)
			{
				bool hasRequiredComponents = true;
				for (int i = 0; i < mask.size(); i++)
				{
					if (mask.test(i) && !registry.hasComponentById(entity, i))
					{
						hasRequiredComponents = false;
						break;
					}
				}
				return hasRequiredComponents;
			}

		private:
			Registry& registry;
			std::bitset<Internal::MaxNumComponents> componentMask;
			bool all;

			friend class Iterator;
		};
	}
}

#endif