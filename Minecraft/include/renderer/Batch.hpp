#ifndef MINECRAFT_BATCH_H
#define MINECRAFT_BATCH_H
#include "core.h"

namespace Minecraft
{
	struct RenderVertex2D
	{
		glm::vec2 position;
		glm::vec4 color;
		uint32 textureSlot;
		glm::vec2 textureCoords;
	};

	struct RenderVertex3D
	{
		glm::vec3 position;
		uint32 textureSlot;
		glm::vec2 textureCoords;
		glm::vec3 normal;
	};

	struct RenderVertexLine
	{
		glm::vec3 start;
		glm::vec3 end;
		float isStart;
		float direction;
		float strokeWidth;
		glm::vec4 color;
	};

	enum class AttributeType
	{
		Float,
		Int,
		Uint
	};

	struct VertexAttribute
	{
		int attributeSlot;
		int numElements;
		AttributeType type;
		uint32 offset;
	};

	namespace _Batch
	{
		const uint32 maxBatchSize = 501;
		const uint32 numTextureGraphicsIds = 16;
		constexpr std::array<int32, numTextureGraphicsIds> textureIndices()
		{
			std::array<int32, numTextureGraphicsIds> res = {};
			for (int32 i = 0; i < numTextureGraphicsIds; i++)
			{
				res[i] = i;
			}
			return res;
		}

		GLenum toGl(AttributeType type);
	}

	template<typename T>
	struct Batch
	{
		uint32 vao;
		uint32 vbo;
		uint32 numVertices;
		int32 zIndex;
		std::array<uint32, _Batch::numTextureGraphicsIds> textureGraphicsIds;
		uint32 dataSize;
		T* data;

		void init(std::initializer_list<VertexAttribute> vertexAttributes)
		{
			dataSize = sizeof(T) * _Batch::maxBatchSize;
			data = (T*)g_memory_allocate(sizeof(T) * _Batch::maxBatchSize);

			// Create the vao
			glCreateVertexArrays(1, &vao);
			glBindVertexArray(vao);

			glGenBuffers(1, &vbo);

			// Allocate space for vbo
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, dataSize, NULL, GL_DYNAMIC_DRAW);

			// Set up the screen vao2D attributes
			// The position doubles as the texture coordinates so we can use the same floats for that
			int attributeSlot = 0;
			for (const auto& attribute : vertexAttributes)
			{
				GLenum type = _Batch::toGl(attribute.type);
				switch (attribute.type)
				{
				case AttributeType::Float:
					glVertexAttribPointer(attribute.attributeSlot, attribute.numElements, type, GL_FALSE, sizeof(T), (void*)(int64)attribute.offset);
					break;
				case AttributeType::Int:
				case AttributeType::Uint:
					glVertexAttribIPointer(attribute.attributeSlot, attribute.numElements, type, sizeof(T), (void*)(int64)attribute.offset);
					break;
				}
				glEnableVertexAttribArray(attribute.attributeSlot);
			}

			for (int i = 0; i < textureGraphicsIds.size(); i++)
			{
				textureGraphicsIds[i] = UINT32_MAX;
			}
			zIndex = 0;
			numVertices = 0;
		}

		void addVertex(const T& vertex)
		{
			g_logger_assert(data != nullptr, "Invalid batch.");
			if (numVertices >= _Batch::maxBatchSize)
			{
				g_logger_warning("Batch ran out of room. I have %d / %d vertices.", numVertices, _Batch::maxBatchSize);
				return;
			}
			if (numVertices < 0)
			{
				g_logger_error("Invalid vertex number.");
				return;
			}

			data[numVertices] = vertex;
			numVertices++;
		}

		void flush()
		{
			if (numVertices <= 0)
			{
				clearTexSlots();
				return;
			}

			// Draw the 3D screen space stuff
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, dataSize, data, GL_DYNAMIC_DRAW);

			glBindVertexArray(vao);
			glDrawArrays(GL_TRIANGLES, 0, numVertices);

			// Clear the batch
			numVertices = 0;
			clearTexSlots();
		}

		void free()
		{
			if (data)
			{
				g_memory_free(data);
				data = nullptr;
				dataSize = 0;
			}
		}

		bool hasTexture(uint32 textureGraphicsId)
		{
			for (int i = 0; i < textureGraphicsIds.size(); i++)
			{
				if (textureGraphicsIds[i] == textureGraphicsId)
				{
					return true;
				}
			}

			return false;
		}

		bool hasTextureRoom3D()
		{
			for (int i = 0; i < textureGraphicsIds.size(); i++)
			{
				if (textureGraphicsIds[i] == UINT32_MAX)
				{
					return true;
				}
			}

			return false;
		}

		// TODO: Change the way we store fonts vs regular textures
		bool hasTextureRoom(bool isFont)
		{
			if (isFont)
			{
				for (int i = 0; i < 8; i++)
				{
					if (textureGraphicsIds[i] == UINT32_MAX)
					{
						return true;
					}
				}
			}
			else
			{
				for (int i = 8; i < 16; i++)
				{
					if (textureGraphicsIds[i] == UINT32_MAX)
					{
						return true;
					}
				}
			}

			return false;
		}

		uint32 getTextureSlot3D(uint32 textureGraphicsId)
		{
			for (int i = 0; i < textureGraphicsIds.size(); i++)
			{
				if (textureGraphicsId == textureGraphicsIds[i] || textureGraphicsIds[i] == UINT32_MAX)
				{
					textureGraphicsIds[i] = textureGraphicsId;
					return i;
				}
			}

			g_logger_warning("Could not find texture id");
			return 0;
		}

		uint32 getTextureSlot(uint32 textureGraphicsId, bool isFont)
		{
			if (isFont)
			{
				for (int i = 0; i < 8; i++)
				{
					if (textureGraphicsId == textureGraphicsIds[i] || textureGraphicsIds[i] == UINT32_MAX)
					{
						textureGraphicsIds[i] = textureGraphicsId;
						return i + 1;
					}
				}
			}
			else
			{
				for (int i = 8; i < 16; i++)
				{
					if (textureGraphicsId == textureGraphicsIds[i] || textureGraphicsIds[i] == UINT32_MAX)
					{
						textureGraphicsIds[i] = textureGraphicsId;
						return i + 1;
					}
				}
			}

			g_logger_warning("Could not find texture id");
			return 0;
		}

		bool hasRoom() const
		{
			return numVertices < _Batch::maxBatchSize;
		}

		bool operator<(const Batch& batch) const
		{
			return (zIndex < batch.zIndex);
		}

	private:
		void clearTexSlots()
		{
			for (int i = 0; i < textureGraphicsIds.size(); i++)
			{
				textureGraphicsIds[i] = UINT32_MAX;
			}
		}
	};
}

#endif 