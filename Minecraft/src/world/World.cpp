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
		static const int numChunks = 64;
		static Chunk loadedChunks[numChunks];

		static int32 seed = 0;

		void init()
		{
			srand((unsigned long)time(NULL));
			seed = (int32)(((float)rand() / (float)RAND_MAX) * 1000.0f);
			Logger::Info("World seed: %d", seed);

			// Initialize blocks
			//TexturePacker::packTextures("C:/dev/C++/MinecraftClone/assets/images/block", "textureFormat.yaml");
			BlockMap::loadBlocks("textureFormat.yaml", "blockFormats.yaml");
			shader = NShader::createShader("C:/dev/C++/MinecraftClone/assets/shaders/default.glsl");
			loadWorldTexture();

			// Create a chunk
			File::createDirIfNotExists("world");
			int chunkIndex = 0;
			Chunk::info();
			for (int z = -2; z < 2; z++)
			{
				for (int x = -2; x < 2; x++)
				{
					Logger::Info("Generating chunk %d", chunkIndex);
					loadedChunks[chunkIndex].generate(x, z, seed);
					//loadedChunks[chunkIndex].deserialize("world", x, z);
					loadedChunks[chunkIndex].generateRenderData();
					loadedChunks[chunkIndex].serialize("world");
					chunkIndex++;
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

			for (int i = 0; i < numChunks; i++)
			{
				NShader::uploadIVec2(shader, "uChunkPos", loadedChunks[i].worldPosition);
				loadedChunks[i].render();
			}
		}

		static void loadWorldTexture()
		{
			worldTexture.internalFormat = ByteFormat::RGBA;
			worldTexture.externalFormat = ByteFormat::RGBA8;
			worldTexture.magFilter = FilterMode::Nearest;
			worldTexture.minFilter = FilterMode::Nearest;
			TextureUtil::Generate(worldTexture, "C:/dev/C++/MinecraftClone/test.png");

			// Upload the world texture
			NShader::uploadInt(shader, "uTexture", worldTexture.graphicsId);
			glUseProgram(shader.programId);
		}
	}
}