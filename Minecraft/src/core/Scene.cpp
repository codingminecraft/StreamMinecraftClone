#include "core/Scene.h"
#include "core/Components.h"
#include "core/File.h"
#include "world/World.h"
#include "world/BlockMap.h"
#include "gui/MainMenu.h"
#include "gui/Gui.h"
#include "renderer/Renderer.h"
#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "utils/Settings.h"
#include "utils/TexturePacker.h"
#include "input/Input.h"
#include "physics/PhysicsComponents.h"
#include "gameplay/CharacterController.h"
#include "gameplay/Inventory.h"

namespace Minecraft
{
	namespace Scene
	{
		// Internal variables
		static SceneType currentScene = SceneType::None;
		static SceneType nextSceneType = SceneType::None;
		static bool changeSceneAtFrameEnd = false;
		static Ecs::Registry* registry;
		static Texture worldTexture;
		static Texture itemTexture;
		static Texture blockItemTexture;

		// TODO: Where does this go??
		static Camera camera;
		static Frustum cameraFrustum;

		// Internal functions
		static void changeSceneInternal();
		static void addCameraToRegistry();

		void init(SceneType type, Ecs::Registry& inRegistry)
		{
			// Initialize all block textures and upload to GPU
			File::createDirIfNotExists("assets/generated");

			const char* packedTexturesFilepath = "assets/generated/packedTextures.png";
			const char* packedItemTexturesFilepath = "assets/generated/packedItemTextures.png";
			TexturePacker::packTextures("assets/images/block", "assets/generated/textureFormat.yaml", packedTexturesFilepath, "Blocks", true);
			TexturePacker::packTextures("assets/images/item", "assets/generated/itemTextureFormat.yaml", packedItemTexturesFilepath, "Items");
			BlockMap::loadBlocks("assets/generated/textureFormat.yaml", "assets/generated/itemTextureFormat.yaml", "assets/custom/blockFormats.yaml");
			BlockMap::uploadTextureCoordinateMapToGpu();

			worldTexture = TextureBuilder()
				.setFormat(ByteFormat::RGBA8_UI)
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setFilepath(packedTexturesFilepath)
				//.generateMipmap()
				.generateTextureObject()
				.bindTextureObject()
				.generate(true);

			itemTexture = TextureBuilder()
				.setFormat(ByteFormat::RGBA8_UI)
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setFilepath(packedItemTexturesFilepath)
				.generateTextureObject()
				.bindTextureObject()
				.generate(true);
			BlockMap::patchTextureMaps(&worldTexture, &itemTexture);

			// Generate all the cube item pictures and pack them into a texture
			const char* blockItemOutput = "assets/generated/blockItems/";
			BlockMap::generateBlockItemPictures("assets/custom/blockFormats.yaml", blockItemOutput);
			TexturePacker::packTextures(blockItemOutput, "assets/generated/blockItemTextureFormat.yaml", "assets/generated/packedBlockItemsTextures.png", "BlockItems", false, 64, 64);
			BlockMap::loadBlockItemTextures("assets/generated/blockItemTextureFormat.yaml");
			blockItemTexture = TextureBuilder()
				.setFormat(ByteFormat::RGBA8_UI)
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Linear)
				.setFilepath("assets/generated/packedBlockItemsTextures.png")
				.generateMipmapFromFile()
				.generateTextureObject()
				.bindTextureObject()
				.generate(true);
			BlockMap::patchBlockItemTextureMaps(&blockItemTexture);
			BlockMap::loadCraftingRecipes("assets/custom/craftingRecipes.yaml");

			registry = &inRegistry;

			changeScene(type);
		}

		void update(float dt)
		{
			Gui::beginFrame();

			switch (currentScene)
			{
			case SceneType::SinglePlayerGame:
			case SceneType::LocalLanGame:
			case SceneType::MultiplayerGame:
				World::update(dt, cameraFrustum, worldTexture);
				break;
			case SceneType::MainMenu:
				MainMenu::update(dt);
				break;
			case SceneType::None:
				break;
			default:
				g_logger_warning("Cannot update unknown scene type %d", currentScene);
				break;
			}

			Renderer::render();

			if (changeSceneAtFrameEnd)
			{
				changeSceneAtFrameEnd = false;
				changeSceneInternal();
			}
		}

		void changeScene(SceneType type)
		{
			// Don't change the scene immediately, instead wait til the end of the frame
			// so we don't disrupt any important simulations
			changeSceneAtFrameEnd = true;
			nextSceneType = type;
		}

		void free(bool freeGlobalResources)
		{
			if (freeGlobalResources)
			{
				worldTexture.destroy();
				itemTexture.destroy();
				blockItemTexture.destroy();
			}

			switch (currentScene)
			{
			case SceneType::SinglePlayerGame:
			case SceneType::LocalLanGame:
			case SceneType::MultiplayerGame:
				World::free();
				break;
			case SceneType::MainMenu:
				MainMenu::free();
				break;
			case SceneType::None:
				break;
			default:
				g_logger_warning("Cannot free unknown scene type %d", currentScene);
				break;
			}
			currentScene = SceneType::None;
		}

		void reloadShaders()
		{
			Renderer::reloadShaders();
			switch (currentScene)
			{
			case SceneType::SinglePlayerGame:
			case SceneType::LocalLanGame:
			case SceneType::MultiplayerGame:
				World::reloadShaders();
				break;
			case SceneType::MainMenu:
				MainMenu::reloadShaders();
				break;
			case SceneType::None:
				break;
			default:
				g_logger_warning("Cannot free unknown scene type %d", currentScene);
				break;
			}
		}

		bool isPlayingGame()
		{
			return currentScene == SceneType::SinglePlayerGame;
		}

		Ecs::Registry* getRegistry()
		{
			return registry;
		}

		Camera& getCamera()
		{
			return camera;
		}

		static void changeSceneInternal()
		{
			free(false);

			registry->clear();
			registry->registerComponent<Transform>("Transform");
			registry->registerComponent<Rigidbody>("Rigidbody");
			registry->registerComponent<BoxCollider>("BoxCollider");
			registry->registerComponent<Tag>("Tag");
			registry->registerComponent<CharacterController>("CharacterController");
			registry->registerComponent<Inventory>("Inventory");
			addCameraToRegistry();

			switch (nextSceneType)
			{
			case SceneType::SinglePlayerGame:
				World::init(*registry);
				break;
			case SceneType::LocalLanGame:
				World::init(*registry, "127.0.0.1", 8080);
				break;
			case SceneType::MultiplayerGame:
				g_logger_warning("Multiplayer games not supported yet.");
				World::init(*registry);
				break;
			case SceneType::MainMenu:
				MainMenu::init();
				break;
			case SceneType::None:
				break;
			default:
				g_logger_warning("Cannot change to unknown scene type %d", nextSceneType);
				break;
			}

			currentScene = nextSceneType;
			nextSceneType = SceneType::None;
		}

		static void addCameraToRegistry()
		{
			// Setup camera
			Ecs::EntityId cameraEntity = registry->createEntity();
			registry->addComponent<Transform>(cameraEntity);
			registry->addComponent<Tag>(cameraEntity);
			Transform& cameraTransform = registry->getComponent<Transform>(cameraEntity);
			cameraTransform.position = glm::vec3(0, 257.0f, 1.0f);
			cameraTransform.orientation = glm::vec3(0.0f, 0.0f, 0.0f);
			camera.fov = 45.0f;
			camera.cameraEntity = cameraEntity;
			Tag& cameraTag = registry->getComponent<Tag>(cameraEntity);
			cameraTag.type = TagType::Camera;

			Renderer::setCameraFrustum(cameraFrustum);
			Renderer::setCamera(camera);
			Input::setProjectionMatrix(camera.calculateHUDProjectionMatrix());
		}
	}
}