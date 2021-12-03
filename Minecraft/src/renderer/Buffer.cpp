#include "renderer/Buffer.hpp"

namespace Minecraft
{
	BufferBuilder::BufferBuilder()
	{
		buffer.type = BufferType::None;
		buffer.graphicsId = UINT32_MAX;
		buffer.usage = BufferUsage::None;
		buffer.maxSize = 0;
	}

	BufferBuilder& BufferBuilder::setBufferType(BufferType type)
	{
		buffer.type = type;
		return *this;
	}

	BufferBuilder& BufferBuilder::setUsageType(BufferUsage usage)
	{
		buffer.usage = usage;
		return *this;
	}

	Buffer BufferBuilder::generate(size_t dataSize, const void* data)
	{
		buffer.generate(dataSize, data);

		return buffer;
	}
}