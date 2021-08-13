#include "core.h"
#include "world/World.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Camera.h"
#include "utils/TexturePacker.h"
#include "core/Input.h"
#include "core/Application.h"
#include "world/BlockMap.h"
#include "gameplay/PlayerController.h"
#include "core/File.h"

#include <random>

namespace Minecraft
{
	namespace World
	{
		// Internal declarations
		static void loadWorldTexture();

		// Members
		static RenderState renderState;
		static Camera camera;
		static Shader shader;
		static Texture worldTexture;
		static std::vector<Chunk> loadedChunks;

		static int32 seed = 0;

		void init()
		{
			srand((unsigned long)time(NULL));
			seed = (int32)(((float)rand() / (float)RAND_MAX) * 1000.0f);
			g_logger_info("World seed: %d", seed);

			// Initialize blocks
			//TexturePacker::packTextures("C:/dev/C++/MinecraftClone/assets/images/block", "textureFormat.yaml");
			BlockMap::loadBlocks("textureFormat.yaml", "blockFormats.yaml");
			BlockMap::uploadTextureCoordinateMapToGpu();
			shader = NShader::createShader("C:/dev/C++/MinecraftClone/assets/shaders/default.glsl");
			loadWorldTexture();

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_BUFFER, BlockMap::getTextureCoordinatesTextureId());
			NShader::uploadInt(shader, "uTexCoordTexture", 1);

			// Create a chunk
			File::createDirIfNotExists("world");
			Chunk::info();
			for (int z = -16; z < 16; z++)
			{
				for (int x = -16; x < 16; x++)
				{
					g_logger_info("Generating chunk (%d, %d)", x, z);
					Chunk chunk;
					chunk.generate(x, z, seed);
					//chunk.deserialize("world", x, z);
					chunk.generateRenderData();
					chunk.serialize("world");
					loadedChunks.push_back(chunk);
				}
			}

			// Setup camera
			camera.position = glm::vec3(0, 257.0f, 1.0f);
			camera.fov = 45.0f;
			camera.orientation = glm::vec3(0.0f, 0.0f, 0.0f);
			PlayerController::init(&camera);
		}

		void update(float dt)
		{
			glm::mat4 projection = camera.calculateProjectionMatrix();
			glm::mat4 view = camera.calculateViewMatrix();
			NShader::uploadMat4(shader, "uProjection", projection);
			NShader::uploadMat4(shader, "uView", view);
			NShader::uploadVec3(shader, "uSunPosition", glm::vec3{ 1, 355, 1 });
			

			PlayerController::update(0.0f);

			if (Input::isKeyPressed(GLFW_KEY_F1))
			{
				Application::lockCursor(false);
			} 
			else if (Input::isKeyPressed(GLFW_KEY_F2))
			{
				Application::lockCursor(true);
			}

			for (const Chunk& chunk : loadedChunks)
			{
				NShader::uploadIVec2(shader, "uChunkPos", chunk.worldPosition);
				chunk.render();
			}
		}

		void cleanup()
		{
			for (Chunk& chunk : loadedChunks)
			{
				chunk.free();
			}
		}

		static void loadWorldTexture()
		{
			worldTexture = TextureBuilder()
				.setFormat(ByteFormat::RGBA8_UI)
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setFilepath("test.png")
				.generate(true);

			// Upload the world texture
			glActiveTexture(GL_TEXTURE0);
			worldTexture.bind();
			NShader::uploadInt(shader, "uTexture", 0);

			glUseProgram(shader.programId);
		}
	}
}