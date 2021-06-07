#ifndef GABE_CPP_UTILS_MEMORY
#define GABE_CPP_UTILS_MEMORY

#include <stdint.h>

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

#define AllocMem(numBytes) CppUtils::Memory::_Allocate(__FILE__, __LINE__, numBytes)
#define ReallocMem(memory, newSize) CppUtils::Memory::_Realloc(__FILE__, __LINE__, memory, newSize)
#define FreeMem(memory) CppUtils::Memory::_Free(__FILE__, __LINE__, memory)

namespace CppUtils
{
	namespace Memory
	{
		void* _Allocate(const char* filename, int line, size_t numBytes);
		void* _Realloc(const char* filename, int line, void* memory, size_t newSize);
		void _Free(const char* filename, int line, void* memory);
		void DumpMemoryLeaks();

		int CompareMem(void* a, void* b, size_t numBytes);
		void ZeroMem(void* memory, size_t numBytes);
		void CopyMem(void* dst, void* src, size_t numBytes);
	}
}

#endif

#ifndef GABE_CPP_UTILS_LOGGER
#define GABE_CPP_UTILS_LOGGER

#define Log(format, ...) _Log(__FILE__, __LINE__, format, __VA_ARGS__)
#define Info(format, ...) _Info(__FILE__, __LINE__, format, __VA_ARGS__)
#define Warning(format, ...) _Warning(__FILE__, __LINE__, format, __VA_ARGS__)
#define Error(format, ...) _Error(__FILE__, __LINE__, format, __VA_ARGS__)
#define Assert(condition, format, ...) _Assert(__FILE__, __LINE__, condition, format, __VA_ARGS__)

namespace CppUtils
{
	namespace Logger
	{
		void _Log(const char* filename, int line, const char* format, ...);
		void _Info(const char* filename, int line, const char* format, ...);
		void _Warning(const char* filename, int line, const char* format, ...);
		void _Error(const char* filename, int line, const char* format, ...);
		void _Assert(const char* filename, int line, int condition, const char* format, ...);
	};
}

#endif

#ifndef GABE_CPP_UTILS_LIST
#define GABE_CPP_UTILS_LIST
#include <typeinfo>

namespace CppUtils
{
	template <typename T>
	using Compare = bool (*)(const T& e1, const T& e2);

	template <typename T>
	bool defaultCompare(const T& e1, const T& e2)
	{
		return memcmp(&e1, &e2, sizeof(T)) == 0;
	}

	template <typename T>
	class List
	{
	public:
		/// <summary>
		///		Creates a list with initial size of 1
		/// </summary>
		List()
		{
			init(1);
		}

		/// <summary>
		///		Creates a list with initial size of "size"
		/// </summary>
		/// <param name="size">The initial size of the list</param>
		List(int size)
		{
			init(size);
		}

		List(const List<T>& other)
		{
			init(other.size());
			replace(0, other.begin(), other.size());
		}

		List(List<T>&& other) noexcept
		{
			m_Data = other.m_Data;
			m_MaxSize = other.m_MaxSize;
			m_NumElements = other.m_NumElements;

			other.m_Data = nullptr;
			other.m_MaxSize = 0;
			other.m_NumElements = 0;
		}

		List<T>& operator=(const List<T>& other)
		{
			if (this != &other)
			{
				if (m_NumElements < other.m_NumElements)
				{
					checkResize(other.m_NumElements - m_NumElements + 1);
				}
				replace(0, other.begin(), other.size());
				m_NumElements = other.m_NumElements;
			}

			return *this;
		}

		List<T>& operator=(List<T>&& other)
		{
			if (this != &other)
			{
				m_MaxSize = other.m_MaxSize;
				m_NumElements = other.m_NumElements;
				m_Data = other.m_Data;

				other.m_MaxSize = 0;
				other.m_NumElements = 0;
				other.m_Data = nullptr;
			}

			return *this;
		}

		~List()
		{
			if (m_Data && m_MaxSize > 0)
			{
				FreeMem(m_Data);
				m_Data = nullptr;
			}
		}

		inline T& operator[](int i)
		{
			return m_Data[i];
		}

		inline const T& operator[](int i) const
		{
			return m_Data[i];
		}

		/// <summary>
		///
		/// </summary>
		/// <returns>Returns const pointer to the beginning of the list.</returns>
		inline T* begin()
		{
			return m_Data;
		}

		/// <summary>
		///
		/// </summary>
		/// <returns>Returns const pointer to the end of the list.</returns>
		inline T* end()
		{
			return m_Data + m_NumElements;
		}

		/// <summary>
		///
		/// </summary>
		/// <returns>Returns const pointer to the beginning of the list.</returns>
		inline const T* begin() const
		{
			return m_Data;
		}

		/// <summary>
		///
		/// </summary>
		/// <returns>Returns const pointer to the end of the list.</returns>
		inline const T* end() const
		{
			return m_Data + m_NumElements;
		}

		/// <summary>
		///		CONSTANT RUNTIME COMPLEXITY. Returns the size of the list. This will always be accurate,
		///		even while iterating, and it is constant runtime complexity because the size is always saved.
		/// </summary>
		/// <returns>The size of the list</returns>
		inline int size() const
		{
			return m_NumElements;
		}

		/// <summary>
		///		Pushes this element to the end of the list
		/// </summary>
		/// <param name="element"> Element to append to the end of this list</param>
		void push(const T& element)
		{
			checkResize(1);
			m_Data[m_NumElements] = element;
			m_NumElements++;
		}

		/// <summary>
		///		Appends the list to the end of this list
		/// </summary>
		/// <param name="list"> The list to append</param>
		void append(const List<T>& list)
		{
			checkResize(list.size());
			memcpy(m_Data + m_NumElements, list.begin(), sizeof(T) * (list.end() - list.begin()));
			m_NumElements += list.size();
		}

		/// <summary>
		///		Insert element at index. Will throw critical assertion if invalid index.
		/// </summary>
		/// <param name="element">Element to insert</param>
		/// <param name="index">Index to insert at</param>
		void insert(const T& element, int index)
		{
			Logger::Assert(index >= 0 && index <= m_NumElements, "Index out of bounds exception. Cannot insert element at '%d' in array of size '%d'.", index, m_NumElements);
			// Make sure we have enough space
			checkResize(1);
			// Only move the data one to the right to make room for the new element
			if (index != m_NumElements)
			{
				memmove(m_Data + index + 1, m_Data + index, sizeof(T) * (m_NumElements - index));
			}
			// Add the element
			m_Data[index] = element;
			m_NumElements++;
		}

		/// <summary>
		///		Insert a range of items at index. Will throw critical assertion if invalid range or index.
		/// </summary>
		/// <param name="first"> The first item in the range to add</param>
		/// <param name="last"> The last item in the range to add</param>
		/// <param name="index"> The index to insert the items at</param>
		void insert(const T* first, const T* last, int index)
		{
			Logger::Assert((last - first) > 0, "Invalid range. T* first must be before T* last.");
			Logger::Assert(index >= 0 && index <= m_NumElements, "Index out of bounds exception. Cannot insert element at '%d' in array of size '%d'.", index, m_NumElements);
			checkResize(last - first);
			// Only move the data 'x' elements to the right if we're not adding to the end of the list to make room for the new range of elements
			if (index != m_NumElements)
			{
				memmove(m_Data + index + (last - first), m_Data + index, sizeof(T) * (m_NumElements - index));
			}
			// Copy the range of data into the new destination
			memcpy(m_Data + index, first, sizeof(T) * (last - first));
			m_NumElements += (last - first);
		}

		/// <summary>
		///		Replace will overwrite anything in the array at the range [index, index + numElementsToOverwrite]
		///		with whatever is stored in the T* pointer as long as index <= m_NumElements.
		/// </summary>
		/// <param name="index"> First element in range to start overwriting</param>
		/// <param name="dataToAdd"> Pointer to sizeof(T)* numElementsToOverwrite elements to copy</param>
		/// <param name="numElementsToOverwrite"> The number of elements to overwrite</param>
		void replace(int index, const T* dataToAdd, int numElementsToOverwrite)
		{
			Logger::Assert(index >= 0 && index <= m_NumElements, "Index out of bounds exception. Cannot place data outside of array bounds, tried to place data at '%d' in array size '%d'", index, m_NumElements);
			checkResize((index + numElementsToOverwrite) - m_MaxSize);
			memcpy(m_Data + index, dataToAdd, sizeof(T) * numElementsToOverwrite);
			if (index + numElementsToOverwrite > m_NumElements)
			{
				m_NumElements = index + numElementsToOverwrite;
			}
		}

		/// <summary>
		///		Removes elements from [startIndex, endIndex] inclusive. Will throw critical assertion if passed invalid range.
		/// </summary>
		/// <param name="startIndex"> First element inclusive</param>
		/// <param name="endIndex"> Last element inclusive</param>
		void removeRange(int startIndex, int endIndex)
		{
			Logger::Assert(startIndex >= 0 && endIndex < m_NumElements&& startIndex < endIndex, "Invalid range. Cannot remove range (%d, %d) in array of size '%d'.", startIndex, endIndex, m_NumElements);
			memmove(m_Data + startIndex, m_Data + endIndex + 1, sizeof(T) * (m_NumElements - endIndex + 1));
			m_NumElements -= (endIndex - startIndex + 1);

			if (m_NumElements < (m_MaxSize / 2))
			{
				m_MaxSize /= 2;
				m_Data = (T*)ReallocMem(m_Data, sizeof(T) * m_MaxSize);
			}
		}

		/// <summary>
		///		Removes elements in between [T* begin, T* end] inclusive. Will throw critical assertion if passed invalid range.
		/// </summary>
		/// <param name="begin"> The pointer to the first element in the range to add</param>
		/// <param name="end"> The pointer to the last element in the range to add</param>
		void removeRange(T* begin, T* end)
		{
			Logger::Assert(begin >= m_Data && end < m_Data + m_NumElements && begin < end, "Invalid range. Cannot remove range (%p, %p), pointers do not exist in this array.", begin, end);
			// We add 2 because we need to include begin, end pointers
			memmove(begin, end + 1, sizeof(T) * (end - begin + 2));
			m_NumElements -= (end - begin + 1);

			if (m_NumElements < (m_MaxSize / 2))
			{
				m_MaxSize /= 2;
				m_Data = (T*)ReallocMem(m_Data, sizeof(T) * m_MaxSize);
			}
		}

		/// <summary>
		///		Removes the element at index
		/// </summary>
		/// <param name="index"> The index of the element to remove. Will throw critical assertion if out of bounds.</param>
		void removeByIndex(int index)
		{
			Logger::Assert(index >= 0 && index < m_NumElements, "Index out of bounds exception. Cannot remove element at '%d' in array of size '%d'.", index, m_NumElements);
			if (index != m_NumElements - 1)
			{
				memmove(m_Data + index, m_Data + index + 1, sizeof(T) * (m_NumElements - index - 1));
			}
			m_NumElements--;

			if (m_NumElements < (m_MaxSize / 2))
			{
				m_MaxSize /= 2;
				m_Data = (T*)ReallocMem(m_Data, sizeof(T) * m_MaxSize);
			}
		}

		/// <summary>
		///		Removes the element if found. Uses the compare function provided or compares the bytes as a default.
		/// </summary>
		/// <param name="element"> Element to remove</param>
		/// <param name="compareFn"> Custom comparator function to use</param>
		void removeByElement(const T& element, Compare<T> compareFn = defaultCompare)
		{
			int index = findIndexOf(element, compareFn);
			if (index >= 0)
			{
				removeByIndex(index);
			}
			else
			{
				Logger::Warning("Could not remove element in array. Element does not exist.");
			}
		}

		/// <summary>
		///		Removes the element if valid. Throws critical assertion if invalid iterator is passed in.
		///		Throws critical assertion if invalid iterator is passed in.
		/// </summary>
		/// <param name="iterator"> The iterator to remove</param>
		/// <returns>The next iterator in the sequence after removing this one.</returns>
		T* removeIter(T* iterator)
		{
			Logger::Assert(iterator >= begin() && iterator < end(), "Invalid iterator. Cannot remove element.");
			int index = iterator - m_Data;
			removeByIndex(index);
			return &(m_Data[index]);
		}

		/// <summary>
		///		Returns reference to element at index
		/// </summary>
		/// <param name="index"> Index of element to get. Will have a critical assertion if index is out of bounds</param>
		/// <returns> A reference to the object at index</returns>
		T& get(int index)
		{
			Logger::Assert(index >= 0 && index < m_NumElements, "Index out of bounds exception. '%d' in array size '%d'.", index, m_NumElements);
			return m_Data[index];
		}

		/// <summary>
		///		Returns const reference to element at index
		/// </summary>
		/// <param name="index"> Index of element to get. Will have a critical assertion if index is out of bounds</param>
		/// <returns>A const reference to the object at index</returns>
		const T& get(int index) const
		{
			Logger::Assert(index >= 0 && index < m_NumElements, "Index out of bounds exception. '%d' in array size '%d'.", index, m_NumElements);
			return m_Data[index];
		}

		/// <summary>
		///		Pops the last element of the array, and returns a copy of the element
		/// </summary>
		/// <returns>A copy of T, the element that was just popped off the list.</returns>
		T pop()
		{
			Logger::Assert(m_NumElements > 0, "Cannot pop empty array.");
			m_NumElements -= 1;
			return m_Data[m_NumElements];
		}

		/// <summary>
		///		Clears the array
		/// </summary>
		/// <param name="freeMemory"> Determines whether or not to reset the memory to sizeof(T) on clear, or retain current size.</param>
		void clear(bool freeMemory = true)
		{
			m_NumElements = 0;
			if (freeMemory)
			{
				if (m_MaxSize != 1)
				{
					m_MaxSize = 1;
					m_Data = (T*)ReallocMem(m_Data, sizeof(T) * m_MaxSize);
				}
			}
		}

		/// <summary>
		///		Finds the index of the element by using the compareFn specified, or by comparing bytes by default.
		/// </summary>
		/// <param name="element"> The element to find the index of</param>
		/// <param name="compareFn"> The comparator function. Defaults to comparing bytes.</param>
		int findIndexOf(const T& element, Compare<T> compareFn = defaultCompare) const
		{
			// TODO: Can this be improved?
			for (int i = 0; i < m_NumElements; i++)
			{
				if (compareFn(m_Data[i], element))
				{
					return i;
				}
			}

			return -1;
		}

		/// <summary>
		///		Resizes the array if the new size is greater then the current maximum capacity.
		/// </summary>
		/// <param name="newSize"> The new minimum number of elements required.</param>
		void resize(int newSize)
		{
			if (newSize > m_MaxSize)
			{
				checkResize(newSize - m_MaxSize);
			}
		}

	private:
		void init(int size)
		{
			const char* typeName = typeid(T).name();
			Logger::Assert(std::is_trivial<T>(), "You cannot use List<%s> with a non-trivial type. This is undefined and will not work correctly, consider using std::vector<%s> instead.", typeName, typeName);
			if (std::is_trivial<T>() && !std::is_pod<T>())
			{
				Logger::Warning("List<%s> is best used with POD types. I would suggest using a std::vector for your use-case, or changing the structure of your object to be standard-layout and trivial.", typeName);
			}
			Logger::Assert(size >= 0, "Cannot initalize a dynamic array of with a negative size.");
			if (size == 0)
			{
				// If a 0 size is passed in initialize an array sizeof 1
				size = 1;
			}

			m_Data = (T*)AllocMem(sizeof(T) * size);
			m_NumElements = 0;
			m_MaxSize = size;
		}

		void checkResize(int numElementsToAdd)
		{
			if (m_NumElements + numElementsToAdd > m_MaxSize)
			{
				m_MaxSize = (m_NumElements + numElementsToAdd) * 2;
				m_Data = (T*)ReallocMem(m_Data, sizeof(T) * m_MaxSize);
			}
		}

	private:
		T* m_Data;
		int m_NumElements;
		int m_MaxSize;
	};
}

#endif

#ifndef GABE_CPP_UTILS_STRING
#define GABE_CPP_UTILS_STRING

namespace CppUtils
{
	namespace String
	{
		const char* CreateString(const char* strToCopy);
		void FreeString(const char* str);
		const char* Substring(const char* strToCopyFrom, int startIndex, int size);
		int StringLength(const char* str);
		bool Compare(const char* str1, const char* str2);
		const char* Join(const char* str1, const char* str2);
		const char* Copy(const char* strToCopy);
		const char* Copy(const char* strToCopy, int numCharactersToCopy);
		bool IsWhitespace(char c);
	}

	class StringBuilder
	{
	public:
		StringBuilder();

		void Append(const char* strToAppend);
		void Append(char character);
		char Pop();
		const char* c_str();
		const char* c_str_copy();
		void StripWhitespace();
		char CharAt(int index);
		int Size();
		void RemoveCharAt(int index);

	private:
		List<char> contents;
	};
}

#endif

#ifndef GABE_CPP_UTILS_HASH_MAP
#define GABE_CPP_UTILS_HASH_MAP

#include <typeinfo>

namespace CppUtils
{
	template<typename K, typename V>
	struct HashEntry
	{
		K key;
		V value;
		uint32 hash;
		bool occupied;
	};

	namespace HashFunctions
	{
		template<typename K>
		uint32 DefaultHash(K object)
		{
			uint64 h = 1125899906842597L; // prime
			size_t len = sizeof(K);
			char* bytePtr = (char*)&object;

			for (int i = 0; i < len; i++)
			{
				h = 31 * h + bytePtr[i];
			}

			return (uint32)h;
		}

#ifdef GABE_CPP_UTILS_IMPL
		uint32 StringHash(const char* string)
		{
			uint64 h = 1125899906842597L; // prime
			size_t len = String::StringLength(string);
			char* bytePtr = (char*)string;

			for (int i = 0; i < len; i++)
			{
				h = 31 * h + bytePtr[i];
			}

			return (uint32)h;
		}
#else 
		uint32 StringHash(const char* string);
#endif
	}

	template<typename K, typename V, uint32(HashFn)(K) = HashFunctions::DefaultHash<K>>
	class HashMap
	{
	public:
		HashMap()
		{
			init(10);
		}

		HashMap(const HashMap<K, V, HashFn>& other)
		{
			init(other.m_MaxSize);
			Memory::CopyMem(m_Data, other.m_Data, sizeof(HashEntry<K, V>) * other.m_MaxSize);
		}

		HashMap(HashMap<K, V, HashFn>&& other) noexcept
		{
			m_Data = other.m_Data;
			m_MaxSize = other.m_MaxSize;
			m_Size = other.m_Size;

			other.m_Data = nullptr;
			other.m_MaxSize = 0;
			other.m_Size = 0;
		}

		HashMap<K, V, HashFn>& operator=(const HashMap<K, V, HashFn>& other)
		{
			if (this != &other)
			{
				if (m_MaxSize != other.m_MaxSize)
				{
					reset(other.m_MaxSize);
				}
				Memory::CopyMem(m_Data, other.m_Data, sizeof(HashEntry<K, V>) * other.m_MaxSize);
				m_Size = other.m_Size;
			}

			return *this;
		}

		HashMap<K, V, HashFn>& operator=(HashMap<K, V, HashFn>&& other)
		{
			if (this != &other)
			{
				m_MaxSize = other.m_MaxSize;
				m_Size = other.m_Size;
				m_Data = other.m_Data;

				other.m_Data = nullptr;
				other.m_Size = 0;
				other.m_MaxSize = 0;
			}

			return *this;
		}

		~HashMap()
		{
			if (m_Data)
			{
				FreeMem(m_Data);
				m_Data = nullptr;
			}
		}

		void insert(K key, V value)
		{
			if (!contains(key))
			{
				checkResize(1);
			}

			uint32 hash = HashFn(key);//hashFn(key);
			uint32 index = getIndex(hash);

#if _DEBUG
			if (m_Data[index].occupied)
			{
				Logger::Assert(Memory::CompareMem(&m_Data[index].key, &key, sizeof(K)) == 0, "An error occured because two different keys have the same hash. This should never be reached.");
			}
#endif

			m_Data[index].key = key;
			m_Data[index].value = value;
			m_Data[index].hash = hash;
			m_Data[index].occupied = true;
			m_Size++;
		}

		V& get(K key)
		{
			Logger::Assert(contains(key), "Invalid key in hash map. Tried to get key that does not exist.");
			uint32 hash = HashFn(key);// hashFn(key);
			uint32 index = getIndex(hash);
			return m_Data[index].value;
		}

		const V& const_get(K key)
		{
			return get(key);
		}

		bool contains(K key)
		{
			uint32 hash = HashFn(key);//hashFn(key);
			uint32 index = getIndex(hash);
			Logger::Assert(index >= 0 && index < m_MaxSize, "Invalid index in hash map.");
			return m_Data[index].occupied;
		}

		//V const& operator[](K key) const
		//{

		//}

		//V& operator[](K key)
		//{

		//}

	private:
		uint32 hashFn(K object)
		{
			uint64 h = 1125899906842597L; // prime
			size_t len = sizeof(K);
			char* bytePtr = (char*)&object;

			for (int i = 0; i < len; i++)
			{
				h = 31 * h + bytePtr[i];
			}

			return (uint32)h;
		}

		uint32 resolveCollision(uint32 index, uint32 hash)
		{
			for (int i = 0; i < m_Size; i++)
			{
				uint32 possibleIndex = (index + i) % m_Size;
				if (!m_Data[possibleIndex].occupied || (m_Data[possibleIndex].occupied && m_Data[possibleIndex].hash == hash))
				{
					return possibleIndex;
				}
			}

			Logger::Assert(false, "Somehow we ran out of room in the hash map. This should never be reached.");
			return 0;
		}

		uint32 getIndex(uint32 hash)
		{
			uint32 index = hash % m_MaxSize;
			if (m_Data[index].occupied && m_Data[index].hash != hash)
			{
				index = resolveCollision(index, hash);
			}
			return index;
		}

		void init(int size)
		{
			const char* kTypename = typeid(K).name();
			const char* vTypename = typeid(V).name();
			Logger::Assert(std::is_pod<K>() && std::is_pod<V>(), "You cannot use HashMap<%s, %s> with a non-trivial type. This is undefined and will not work correctly, consider using std::unordered_map<%s, %s> instead.", kTypename, vTypename, kTypename, vTypename);
			Logger::Assert(size >= 0, "Cannot initalize a HashMap with a negative size.");
			if (size == 0)
			{
				// If a 0 size is passed in initialize an array sizeof 10
				size = 10;
			}

			m_Data = (HashEntry<K, V>*)AllocMem(sizeof(HashEntry<K, V>) * size);
			Memory::ZeroMem(m_Data, sizeof(HashEntry<K, V>) * size);
			m_Size = 0;
			m_MaxSize = size;
		}

		void reset(int newMaxSize)
		{
			m_Data = (HashEntry<K, V>*)ReallocMem(m_Data, sizeof(HashEntry<K, V>) * newMaxSize);
			Memory::ZeroMem(m_Data, sizeof(HashEntry<K, V>) * newMaxSize);
			m_Size = 0;
			m_MaxSize = newMaxSize;
		}

		void checkResize(int numElementsToAdd)
		{
			if (m_Size + numElementsToAdd > m_MaxSize)
			{
				m_MaxSize = (m_Size + numElementsToAdd) * 2;
				HashEntry<K, V>* oldData = m_Data;
				m_Data = (HashEntry<K, V>*)AllocMem(sizeof(HashEntry<K, V>) * m_MaxSize);
				Memory::ZeroMem(m_Data, sizeof(HashEntry<K, V>) * m_MaxSize);

				for (int i = 0; i < m_Size; i++)
				{
					insert(oldData[i].key, oldData[i].value);
				}

				FreeMem(oldData);
			}
		}

	private:
		uint32 m_Size;
		uint32 m_MaxSize;
		HashEntry<K, V>* m_Data;
	};
}

#endif 


#ifdef GABE_CPP_UTILS_IMPL

#include <memory>

namespace CppUtils
{
	namespace Memory
	{
#if _DEBUG
		struct DebugMemoryAllocation
		{
			const char* FileAllocator;
			int FileAllocatorLine;
			int References;
			void* Memory;

			bool operator==(const DebugMemoryAllocation& other) const
			{
				return other.Memory == this->Memory;
			}
		};

		static List<DebugMemoryAllocation> InternalAllocations = List<DebugMemoryAllocation>(10);
#endif

		void* _Allocate(const char* filename, int line, size_t numBytes)
		{
			void* memory = malloc(numBytes);
#if _DEBUG
			// If we are in a debug build, track all memory allocations to see if we free them all as well
			auto iterator = std::find(InternalAllocations.begin(), InternalAllocations.end(), DebugMemoryAllocation{ filename, line, 0, memory });
			if (iterator == InternalAllocations.end())
			{
				InternalAllocations.push({ filename, line, 1, memory });
			}
			else
			{
				if (iterator->References <= 0)
				{
					iterator->References++;
				}
				else
				{
					Logger::Error("Tried to allocate memory that has already been allocated... This should never be hit. If it is, we have a problem.");
				}
			}
#endif
			return memory;
		}

		void* _Realloc(const char* filename, int line, void* oldMemory, size_t numBytes)
		{
			void* newMemory = realloc(oldMemory, numBytes);
#if _DEBUG
			// If we are in a debug build, track all memory allocations to see if we free them all as well
			auto newMemoryIter = std::find(InternalAllocations.begin(), InternalAllocations.end(), DebugMemoryAllocation{ filename, line, 0, newMemory });
			auto oldMemoryIter = std::find(InternalAllocations.begin(), InternalAllocations.end(), DebugMemoryAllocation{ filename, line, 0, oldMemory });
			if (newMemoryIter != oldMemoryIter)
			{
				// Realloc could not expand the current pointer, so it allocated a new memory block
				if (oldMemoryIter == InternalAllocations.end())
				{
					Logger::Error("Tried to realloc invalid memory in '%s' line: %d.", filename, line);
				}
				else
				{
					oldMemoryIter->References--;
				}

				if (newMemoryIter == InternalAllocations.end())
				{
					InternalAllocations.push({ filename, line, 1, newMemory });
				}
				else
				{
					if (newMemoryIter->References <= 0)
					{
						newMemoryIter->References++;
					}
					else
					{
						Logger::Error("Tried to allocate memory that has already been allocated... This should never be hit. If it is, we have a problem.");
					}
				}
			}
			// If realloc expanded the memory in-place, then we don't need to do anything because no "new" memory locations were allocated
#endif
			return newMemory;
		}

		void _Free(const char* filename, int line, void* memory)
		{
#if _DEBUG
			auto iterator = std::find(InternalAllocations.begin(), InternalAllocations.end(), DebugMemoryAllocation{ filename, line, 0, memory });
			if (iterator == InternalAllocations.end())
			{
				if (memory != InternalAllocations.begin())
				{
					Logger::Error("Tried to free invalid memory that was never allocated.");
					Logger::Error("Code that attempted to free: '%s' line: %d", filename, line);
				}
			}
			else if (iterator->References <= 0)
			{
				Logger::Error("Tried to free memory that has already been freed.");
				Logger::Error("Code that attempted to free: '%s' line: %d", filename, line);
				Logger::Error("Code that allocated the memory block: '%s' line: %d", iterator->FileAllocator, iterator->FileAllocatorLine);
			}
			else
			{
				iterator->References--;
			}
#endif
			// When debug is turned off we literally just free the memory, so it will through a segfault if a
			// faulty release build was published
			free(memory);
		}

		void DumpMemoryLeaks()
		{
#if _DEBUG
			for (auto& alloc : InternalAllocations)
			{
				if (alloc.References > 0)
				{
					Logger::Warning("Application ended execution and did not free memory allocated at: '%s' line: %d", alloc.FileAllocator, alloc.FileAllocatorLine);
				}
			}
#endif
		}

		int CompareMem(void* a, void* b, size_t numBytes)
		{
			return memcmp(a, b, numBytes);
		}

		void ZeroMem(void* memory, size_t numBytes)
		{
			memset(memory, 0, numBytes);
		}

		void CopyMem(void* dst, void* src, size_t numBytes)
		{
			memcpy(dst, src, numBytes);
		}
	}
}

#include <stdio.h>
#include <varargs.h>
#include <stdarg.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <crtdbg.h>
#endif

namespace CppUtils
{
	namespace Logger
	{
		void _Log(const char* filename, int line, const char* format, ...)
		{
#ifdef _WIN32
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_BLUE | FOREGROUND_GREEN);
			printf("%s (line %d) Log: \n", filename, line);
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 0x0F);
			printf("-> ");

			va_list args;
			va_start(args, format);
			vprintf(format, args);
			va_end(args);

			printf("\n");
#else
			printf("%s (line %d) Log: \n", filename, line);
			printf("-> ");

			va_list args;
			va_start(args, format);
			vprintf(format, args);
			va_end(args);

			printf("\n");
#endif
		}

		void _Info(const char* filename, int line, const char* format, ...)
		{
#ifdef _WIN32
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN);
			printf("%s (line %d) Info: \n", filename, line);
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 0x0F);
			printf("-> ");

			va_list args;
			va_start(args, format);
			vprintf(format, args);
			va_end(args);

			printf("\n");
#else
			printf("%s (line %d) Info: \n", filename, line);
			printf("-> ");

			va_list args;
			va_start(args, format);
			vprintf(format, args);
			va_end(args);

			printf("\n");
#endif
		}

		void _Warning(const char* filename, int line, const char* format, ...)
		{
#ifdef _WIN32
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_RED);
			printf("%s (line %d) Warning: \n", filename, line);
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 0x0F);
			printf("-> ");

			va_list args;
			va_start(args, format);
			vprintf(format, args);
			va_end(args);

			printf("\n");
#else
			printf("%s (line %d) Warning: \n", filename, line);
			printf("-> ");

			va_list args;
			va_start(args, format);
			vprintf(format, args);
			va_end(args);

			printf("\n");
#endif
		}

		void _Error(const char* filename, int line, const char* format, ...)
		{
#ifdef _WIN32
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
			printf("%s (line %d) Error: \n", filename, line);
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 0x0F);
			printf("-> ");

			va_list args;
			va_start(args, format);
			vprintf(format, args);
			va_end(args);

			printf("\n");
#else
			printf("%s (line %d) Error: \n", filename, line);
			printf("-> ");

			va_list args;
			va_start(args, format);
			vprintf(format, args);
			va_end(args);

			printf("\n");
#endif
		}

		void _Assert(const char* filename, int line, int condition, const char* format, ...)
		{
			if (!condition)
			{
#ifdef _WIN32
				SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
				printf("%s (line %d) Assertion Failure: \n", filename, line);
				SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 0x0F);
				printf("-> ");

				va_list args;
				va_start(args, format);
				vprintf(format, args);
				va_end(args);

				printf("\n");
				_CrtDbgBreak();
#else
				printf("%s (line %d) Assertion Failure: \n", filename, line);
				printf("-> ");

				va_list args;
				va_start(args, format);
				vprintf(format, args);
				va_end(args);

				printf("\n");
#endif
				exit(-1);
			}
		}
	}
}

#include <cstring>

namespace CppUtils
{
	namespace String
	{
		static const char* DefaultEmptyString = "";

		const char* CreateString(const char* strToCopy)
		{
			return Copy(strToCopy);
		}

		const char* Copy(const char* strToCopy)
		{
			if (!Compare(strToCopy, DefaultEmptyString))
			{
				size_t size = StringLength(strToCopy);
				size_t sizeWithNullByte = size + (unsigned long long)1;
				char* newStr = (char*)AllocMem(sizeWithNullByte);

				if (newStr)
				{
					memcpy(newStr, strToCopy, size);
					newStr[size] = '\0';
				}
				else
				{
					Logger::Error("Failed to allocate memory for string.");
					return DefaultEmptyString;
				}
				return newStr;
			}

			return DefaultEmptyString;
		}

		const char* Copy(const char* strToCopy, int numCharactersToCopy)
		{
			if (Compare(strToCopy, DefaultEmptyString) != 0)
			{
				size_t length = StringLength(strToCopy);
				if (numCharactersToCopy <= length)
				{
					size_t sizeWithNullByte = numCharactersToCopy + (unsigned long long)1;
					char* newStr = (char*)AllocMem(sizeWithNullByte);

					if (newStr)
					{
						memcpy(newStr, strToCopy, numCharactersToCopy);
						newStr[numCharactersToCopy] = '\0';
					}
					else
					{
						Logger::Error("Failed to allocate memory for string.");
						return DefaultEmptyString;
					}
					return newStr;
				}
			}

			return DefaultEmptyString;
		}

		const char* Substring(const char* strToCopyFrom, int startIndex, int size)
		{
			if (strcmp(strToCopyFrom, DefaultEmptyString))
			{
				size_t strToCopyFromSize = StringLength(strToCopyFrom);
				size_t sizeWithNullByte = size + (unsigned long long)1;
				char* newStr = (char*)AllocMem(sizeWithNullByte);

				if (newStr)
				{
					if (startIndex >= 0 && startIndex + size <= strToCopyFromSize)
					{
						memcpy(newStr, &(strToCopyFrom[startIndex]), sizeof(char) * size);
						newStr[size] = '\0';
					}
					else
					{
						Logger::Error("Invalid range for substring.");
						return DefaultEmptyString;
					}
				}
				else
				{
					Logger::Error("Failed to allocate memory for string.");
					return DefaultEmptyString;
				}
				return newStr;
			}

			return DefaultEmptyString;
		}

		int StringLength(const char* str)
		{
			return (int)strlen(str);
		}

		bool Compare(const char* str1, const char* str2)
		{
			return strcmp(str1, str2) == 0;
		}

		const char* Join(const char* str1, const char* str2)
		{
			size_t strLength1 = strlen(str1);
			size_t strLength2 = strlen(str2);
			size_t newStrLength = strLength1 + strLength2;
			char* newStr = (char*)AllocMem(sizeof(char) * (newStrLength + 1));
			if (newStr)
			{
				memcpy(newStr, str1, sizeof(char) * strLength1);
				memcpy(newStr + strLength1, str2, sizeof(char) * strLength2);
				newStr[newStrLength] = '\0';
				return newStr;
			}

			Logger::Error("Failed to allocate memory for string.");
			return nullptr;
		}

		void FreeString(const char* str)
		{
			if (str && str != DefaultEmptyString && !Compare(str, ""))
			{
				FreeMem((void*)str);
			}
		}

		bool IsWhitespace(char c)
		{
			return c == ' ' || c == '\t' || c == '\n' || c == '\r';
		}
	}

	StringBuilder::StringBuilder()
	{
		contents = List<char>(10);
	}

	void StringBuilder::Append(const char* strToAppend)
	{
		const char* c = strToAppend;
		while (*c != '\0')
		{
			contents.push(*c);
			c++;
		}
	}

	char StringBuilder::Pop()
	{
		return contents.pop();
	}

	void StringBuilder::Append(char character)
	{
		contents.push(character);
	}

	const char* StringBuilder::c_str()
	{
		// This will put a '\0' nullbyte at the end of the string
		// and then remove it from the array, but the nullbyte will
		// still be there until another element is pushed to the stack
		contents.push('\0');
		contents.pop();
		return contents.begin();
	}

	const char* StringBuilder::c_str_copy()
	{
		const char* copy = String::CreateString(c_str());
		return copy;
	}

	void StringBuilder::StripWhitespace()
	{
		for (int i = 0; i < contents.size(); i++)
		{
			if (String::IsWhitespace(contents[i]))
			{
				contents.removeByIndex(i);
				i--;
			}
			else
			{
				break;
			}
		}

		for (int i = contents.size() - 1; i >= 0; i--)
		{
			if (String::IsWhitespace(contents[i]))
			{
				contents.pop();
			}
			else
			{
				break;
			}
		}
	}

	char StringBuilder::CharAt(int index)
	{
		Logger::Assert(index >= 0 && index < contents.size(), "Invalid char index '%d' in string builder of size '%d'", index, contents.size());
		return contents[index];
	}

	void StringBuilder::RemoveCharAt(int index)
	{
		Logger::Assert(index >= 0 && index < contents.size(), "Invalid char index '%d' in string builder of size '%d'", index, contents.size());
		contents.removeByIndex(index);
	}

	int StringBuilder::Size()
	{
		return contents.size();
	}
}

#endif // GABE_CPP_UTILS_IMPL