#ifndef MINECRAFT_POOL_H
#define MINECRAFT_POOL_H
#include "core.h"

namespace Minecraft
{
	template<typename T, const int NumPools>
	class Pool
	{
	public:
		Pool()
		{
			data = nullptr;
			dataLength = 0;
			NumPools = 0;
			_poolSize = 0;
			poolsBeingUsed.reset();
		}

		Pool(uint32 poolSize)
		{
			data = (T*)g_memory_allocate(sizeof(T) * poolSize * NumPools);
			dataLength = NumPools * poolSize;
			poolsBeingUsed.reset();
			_poolSize = poolSize;
		}

		~Pool()
		{
			if (data != nullptr)
			{
				g_memory_free(data);
				data = nullptr;
				dataLength = 0;
				_poolSize = 0;
			}
		}

		T* operator[](int poolIndex)
		{
			g_logger_assert(poolIndex >= 0 && poolIndex < NumPools, "Pool index '%d' out of bounds in pool with size '%d'.", poolIndex, NumPools);
			return data + (_poolSize * poolIndex);
		}

		const T* operator[](int poolIndex) const
		{
			g_logger_assert(poolIndex >= 0 && poolIndex < NumPools, "Pool index '%d' out of bounds in pool with size '%d'.", poolIndex, NumPools);
			return data+ (_poolSize * poolIndex);
		}

		T* getNewPool()
		{
			for (int i = 0; i < poolsBeingUsed.size(); i++)
			{
				if (!poolsBeingUsed.test(i))
				{
					poolsBeingUsed.set(i, true);
					return data + (_poolSize * index);
				}
			}
		}

		void freePool(uint32 poolIndex)
		{
			g_logger_assert(poolIndex >= 0 && poolIndex < NumPools, "Pool index '%d' out of bounds in pool with size '%d'.", poolIndex, NumPools);
			poolsBeingUsed.set(poolIndex, false);
		}

		uint32 size() const
		{
			return NumPools;
		}

		uint32 poolSize() const
		{
			return _poolSize;
		}

		uint64 totalSize() const
		{
			return dataLength * sizeof(T);
		}

	private:
		std::bitset<NumPools> poolsBeingUsed;
		uint64 dataLength;
		uint32 _poolSize;
		T* data;
	};
}

#endif 