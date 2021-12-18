#include "utils/Constants.h"
#include "utils/YamlExtended.h"
#include "world/BlockMap.h"
#include "core/File.h"
#include "renderer/Renderer.h"

namespace Minecraft
{
	struct Pixel
	{
		uint8 r;
		uint8 g;
		uint8 b;
		uint8 a;
	};

	namespace Vertices
	{
		static robin_hood::unordered_flat_map<std::string, std::vector<VoxelVertex>> itemModels;

		const float fullScreenSpaceRectangle[24] = {
			1.0f, 1.0f, 1.0f, 1.0f,     // Top-right pos and uvs
			-1.0f, 1.0f, 0.0f, 1.0f,   // Top-left pos and uvs
			1.0f, -1.0f, 1.0f, 0.0f,   // Bottom-right pos and uvs

			1.0f, -1.0f, 1.0f, 0.0f,   // Bottom-right pos and uvs
			-1.0f, 1.0f, 0.0f, 1.0f,   // Top-left pos and uvs
			-1.0f, -1.0f, 0.0f, 0.0f  // Bottom-left pos and uvs
		};

		uint32 fullScreenSpaceRectangleVao = UINT32_MAX;
		static uint32 fullScreenSpaceRectangleVbo = UINT32_MAX;

		static void createItemModels(const char* blockFormatConfig);
		static void createItemModel(const std::string& itemPicture, const std::string& itemName);

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

			createItemModels("assets/custom/blockFormats.yaml");
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

		Model getItemModel(const std::string& itemName)
		{
			const std::vector<VoxelVertex>& itemVertices = itemModels[itemName];
			return {
				itemVertices.data(),
				(int)itemVertices.size()
			};
		}

		static void createItemModels(const char* blockFormatConfig)
		{
			YAML::Node blockFormat = YamlExtended::readFile(blockFormatConfig);

			for (auto block : blockFormat)
			{
				g_logger_assert(block.second["id"].IsDefined(), "All blocks must have a block id defined. Block '%s' does not have an id.", block.first.as<std::string>().c_str());
				int id = block.second["id"].as<int>();
				if (block.second["isItem"].IsDefined())
				{
					std::string itemPictureName = block.second["itemPicture"].IsDefined() ? block.second["itemPicture"].as<std::string>() : "null";
					std::string filepath = "assets/images/item/" + itemPictureName + ".png";
					if (File::isFile(filepath.c_str()))
					{
						createItemModel(filepath, itemPictureName);
					}
					else
					{
						g_logger_error("Could not generate item model for '%s'", filepath.c_str());
					}
				}
			}
		}

		static void createItemModel(const std::string& itemPicture, const std::string& itemName)
		{
			int width, height, channels;
			Pixel* pixels = (Pixel*)stbi_load(itemPicture.c_str(), &width, &height, &channels, 4);
			std::vector<VoxelVertex> vertices;
			float normalizedPixHalfWidth = (1.0f / (float)width) / 2.0f;
			float normalizedPixHalfHeight = (1.0f / (float)height) / 2.0f;
			g_logger_assert(normalizedPixHalfHeight == normalizedPixHalfWidth, "All item pictures must be square.");
			glm::vec3 normalizedPixSize = glm::vec3(normalizedPixHalfWidth, normalizedPixHalfHeight, 0.0f);

			for (int y = 0; y < height; y++)
			{
				for (int x = 0; x < width; x++)
				{
					Pixel* pixel = pixels + (x + (y * width));
					glm::ivec2 pixelPos = glm::ivec2(x, y);
					if (pixel->a != 0)
					{
						float normalizedX = (float)x / (float)width;
						float normalizedY = (float)y / (float)height;
						glm::vec3 currentPos = glm::vec3(normalizedX, normalizedY, 0.0f);

						// Generate front and back faces of opaque pixel regardless
						{
							VoxelVertex quadVerts[4];
							const glm::ivec2 corners[4] = {
								INormals2::Up + INormals2::Right,
								INormals2::Down + INormals2::Right,
								INormals2::Down + INormals2::Left,
								INormals2::Up + INormals2::Left
							};
							int i = 0;
							for (const glm::ivec2& direction : corners)
							{
								quadVerts[i].position = currentPos + (glm::vec3(direction.x, direction.y, 0.0f) * normalizedPixSize);
								quadVerts[i].color = glm::u8vec4(pixel->r, pixel->g, pixel->b, pixel->a);
								i++;
							}

							// Front face
							vertices.push_back(quadVerts[2]);
							vertices.push_back(quadVerts[3]);
							vertices.push_back(quadVerts[0]);

							vertices.push_back(quadVerts[2]);
							vertices.push_back(quadVerts[0]);
							vertices.push_back(quadVerts[1]);

							for (int i = 0; i < 4; i++)
							{
								quadVerts[i].position.z -= normalizedPixHalfHeight;
							}

							// Back face
							vertices.push_back(quadVerts[2]);
							vertices.push_back(quadVerts[1]);
							vertices.push_back(quadVerts[0]);

							vertices.push_back(quadVerts[3]);
							vertices.push_back(quadVerts[2]);
							vertices.push_back(quadVerts[0]);
						}

						// Check which sides to extrude on z-axis
						bool extrudeZ[4] = { false, false, false, false };
						int i = 0;
						for (const glm::ivec2& direction : INormals2::CardinalDirections)
						{
							glm::ivec2 neighborPos = pixelPos + direction;
							if (neighborPos.x < width && neighborPos.y < height &&
								neighborPos.x >= 0 && neighborPos.y >= 0)
							{
								Pixel* neighborPixel = pixels + (neighborPos.x + (neighborPos.y * width));
								if (neighborPixel->a == 0)
								{
									// If the neighbor pixel exists and is transparent, extrude in the z-direction
									extrudeZ[i] = true;
								}
							}
							else
							{
								extrudeZ[i] = true;
							}
							i++;
						}
					}
				}
			}

			itemModels[itemName] = vertices;
			stbi_image_free(pixels);
		}
	}
}