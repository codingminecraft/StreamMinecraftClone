#include "renderer/Batch.hpp"

namespace Minecraft
{
	namespace _Batch
	{
		GLenum toGl(AttributeType type)
		{
			switch (type)
			{
			case AttributeType::Float:
				return GL_FLOAT;
			case AttributeType::Int:
				return GL_INT;
			case AttributeType::Uint:
				return GL_UNSIGNED_INT;
			}

			return GL_NONE;
		}
	}
}