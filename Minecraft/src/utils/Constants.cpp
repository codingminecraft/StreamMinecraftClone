#include "utils/Constants.h"

namespace Minecraft
{
	namespace Vertices
	{
		const float fullScreenSpaceRectangle[24] = {
			1.0f, 1.0f, 1.0f, 1.0f,     // Top-right pos and uvs
			-1.0f, 1.0f, 0.0f, 1.0f,   // Top-left pos and uvs
			1.0f, -1.0f, 1.0f, 0.0f,   // Bottom-right pos and uvs

			1.0f, -1.0f, 1.0f, 0.0f,   // Bottom-right pos and uvs
			-1.0f, 1.0f, 0.0f, 1.0f,   // Top-left pos and uvs
			-1.0f, -1.0f, 0.0f, 0.0f  // Bottom-left pos and uvs
		};

		extern uint32 fullScreenSpaceRectangleVao = UINT32_MAX;
		static uint32 fullScreenSpaceRectangleVbo = UINT32_MAX;

		void init()
		{
			glGenVertexArrays(1, &fullScreenSpaceRectangleVao);
			glBindVertexArray(fullScreenSpaceRectangleVao);

			glGenBuffers(1, &fullScreenSpaceRectangleVbo);
			glBindBuffer(GL_ARRAY_BUFFER, fullScreenSpaceRectangleVbo);

			glBufferData(GL_ARRAY_BUFFER, sizeof(fullScreenSpaceRectangle), fullScreenSpaceRectangle, GL_STATIC_DRAW);

			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)0);
			glEnableVertexAttribArray(0);

			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)(sizeof(float) * 2));
			glEnableVertexAttribArray(1);
		}

		void free()
		{
			if (fullScreenSpaceRectangleVao != UINT32_MAX)
			{
				glDeleteVertexArrays(1, &fullScreenSpaceRectangleVao);
			}

			if (fullScreenSpaceRectangleVbo != UINT32_MAX)
			{
				glDeleteBuffers(1, &fullScreenSpaceRectangleVbo);
			}
		}
	}
}