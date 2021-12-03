#ifndef MINECRAFT_BUFFER_H
#define MINECRAFT_BUFFER_H
#ifdef _OPENGL
#include "core.h"

namespace Minecraft
{
	enum class BufferType : uint8
	{
		None=0,

		IndirectBuffer,
		ArrayBuffer,
	};

	enum class BufferUsage : uint8
	{
		None=0,

		DynamicDraw,
	};

	namespace BufferUtil
	{
		constexpr uint32 toGlBufferType(BufferType type)
		{
			switch (type)
			{
			case BufferType::IndirectBuffer:
				return GL_DRAW_INDIRECT_BUFFER;
			case BufferType::ArrayBuffer:
				return GL_ARRAY_BUFFER;
			default:
				g_logger_error("Unknown buffer type '%s'", magic_enum::enum_name(type).data());
				return GL_NONE;
			}
		}

		constexpr uint32 toGlUsageType(BufferUsage type)
		{
			switch (type)
			{
			case BufferUsage::DynamicDraw:
				return GL_DYNAMIC_DRAW;
			default:
				g_logger_error("Unknown buffer type '%s'", magic_enum::enum_name(type).data());
				return GL_NONE;
			}
		}
	}

	struct Buffer
	{
		BufferType type;
		BufferUsage usage;
		uint32 graphicsId;
		size_t maxSize;

		inline void bind() 
		{ 
			g_logger_assert(graphicsId != UINT32_MAX, "You tried to bind an invalid buffer.");
			glBindBuffer(BufferUtil::toGlBufferType(type), graphicsId); 
		}

		inline void generate(size_t dataSize, const void* data)
		{ 
			g_logger_assert(graphicsId == UINT32_MAX, "You tried to generate a new buffer in a buffer that hasn't been freed yet. This could lead to memory leaks on the GPU.");
			glGenBuffers(1, &graphicsId);
			bind();
			glBufferData(BufferUtil::toGlBufferType(type), dataSize, data, BufferUtil::toGlUsageType(usage));
			maxSize = dataSize;
		}

		inline void bufferSubData(size_t offset, size_t dataSize, const void* data)
		{
			bind();
			g_logger_assert(offset + dataSize <= maxSize, "You buffered too much data. You tried to buffer '%zu' bytes in a buffer of size '%zu' bytes", dataSize, maxSize);
			glBufferSubData(BufferUtil::toGlBufferType(type), offset, dataSize, data);
		}

		inline void free()
		{
			g_logger_assert(graphicsId != UINT32_MAX, "You tried to free an invalid buffer.");
			glDeleteBuffers(1, &graphicsId);
		}
	};

	class BufferBuilder
	{
	public:
		BufferBuilder();

		BufferBuilder& setBufferType(BufferType type);
		BufferBuilder& setUsageType(BufferUsage usage);
		
		Buffer generate(size_t dataSize, const void* data = nullptr);

	private:
		Buffer buffer;
	};
}

#endif // _OPENGL
#endif // MINECRAFT_BUFFER_H