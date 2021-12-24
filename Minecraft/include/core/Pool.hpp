#ifndef MINECRAFT_POOL_H
#define MINECRAFT_POOL_H
#include "core.h"

namespace Minecraft
{
	template<typename T>
	class Pool
	{
	public:
		Pool()
		{
			numPools = 0;
			data = nullptr;
			dataLength = 0;
			_poolSize = 0;
			std::lock_guard<std::mutex> lock(freeListMtx);
			freeList = nullptr;
			freeListSize = 0;
			freeListStart = 0;
		}

		Pool(uint32 poolSize, uint32 numPools)
		{
			this->numPools = numPools;
			data = (T*)g_memory_allocate(sizeof(T) * poolSize * numPools);
			dataLength = numPools * poolSize;
			_poolSize = poolSize;

			// Initialize the free list
			std::lock_guard<std::mutex> lock(freeListMtx);
			freeListSize = numPools;
			freeListStart = 0;
			freeList = (T**)g_memory_allocate(sizeof(T*) * numPools);
			for (uint32 i = 0; i < numPools; i++)
			{
				freeList[i] = data + (_poolSize * i);
			}
		}

		~Pool()
		{
			if (data != nullptr)
			{
				g_memory_free(data);
				data = nullptr;
				dataLength = 0;
				_poolSize = 0;
				numPools = 0;

				g_memory_free(freeList);
				freeList = nullptr;
				freeListSize = 0;
				freeListStart = 0;
			}
		}

		T* operator[](int poolIndex)
		{
			g_logger_assert(poolIndex >= 0 && poolIndex < (int)numPools, "Pool index '%d' out of bounds in pool with size '%d'.", poolIndex, numPools);
			return data + (_poolSize * poolIndex);
		}

		const T* operator[](int poolIndex) const
		{
			g_logger_assert(poolIndex >= 0 && poolIndex < (int)numPools, "Pool index '%d' out of bounds in pool with size '%d'.", poolIndex, numPools);
			return data + (_poolSize * poolIndex);
		}

		T* getNewPool()
		{
			std::lock_guard<std::mutex> lock(freeListMtx);
			if (freeListSize > 0)
			{
				T* nextPool = freeList[freeListStart];
				freeList[freeListStart] = nullptr;
				freeListStart = (freeListStart + 1) % numPools;
				freeListSize--;
				return nextPool;
			}

			g_logger_error("Ran out of pools.");
			return nullptr;
		}

		void freePool(uint32 poolIndex)
		{
			g_logger_assert(poolIndex >= 0 && poolIndex < numPools, "Pool index '%d' out of bounds in pool with size '%d'.", poolIndex, numPools);
			std::lock_guard<std::mutex> lock(freeListMtx);
			uint32 nextIndex = (freeListStart + freeListSize) % numPools;
			freeList[nextIndex] = (T*)(data + (_poolSize * poolIndex));
			freeListSize++;
		}

		void freePool(T* pool)
		{
			g_logger_assert(pool >= data && pool <= data + (_poolSize * numPools), "Data '%zu' does not exist in this pool.", pool);
			uint32 poolIndex = (uint32)((pool - data) / _poolSize);
			freePool(poolIndex);
		}

		uint32 count()
		{
			std::lock_guard<std::mutex> lock(freeListMtx);
			return freeListSize;
		}

		uint32 size() const
		{
			return numPools;
		}

		uint32 poolSize() const
		{
			return _poolSize;
		}

		uint64 totalSize() const
		{
			return dataLength * sizeof(T);
		}

		bool empty()
		{
			std::lock_guard<std::mutex> lock(freeListMtx);
			return freeListSize == 0;
		}

	private:
		std::mutex freeListMtx;
		uint32 freeListStart;
		uint32 freeListSize;
		T** freeList;

		uint64 dataLength;
		uint32 _poolSize;
		uint32 numPools;
		T* data;
	};

}

#endif 