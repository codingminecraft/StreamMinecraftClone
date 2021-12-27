#include "core/Scene.h"
#include "core/Components.h"
#include "core/File.h"
#include "core/Application.h"
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
#include "gameplay/PlayerController.h"

namespace Minecraft
{
	namespace Scene
	{
		// Global variables
		bool serializeEvents = false;
		bool playFromEventFile = false;

		// Internal variables
		static SceneType currentScene = SceneType::None;
		static SceneType nextSceneType = SceneType::None;
		static bool changeSceneAtFrameEnd = false;
		static Ecs::Registry* registry;
		static Texture worldTexture;
		static Texture itemTexture;
		static Texture blockItemTexture;
		static std::queue<GEvent> events;
		static FILE* serializedEventFile;

		// TODO: Where does this go??
		static Camera camera;
		static Frustum cameraFrustum;

		// Internal functions
		static void changeSceneInternal();
		static void addCameraToRegistry();

		static void processEvents();
		static void serializeEvent(const GEvent& event);
		static void processEvent(GEventType type, void* data, size_t sizeOfData);
		static size_t getEventSize(GEventType type);

		void init(SceneType type, Ecs::Registry& inRegistry)
		{
			// Initialize all block textures and upload to GPU
			File::createDirIfNotExists("assets/generated");
			events = std::queue<GEvent>();
			serializedEventFile = nullptr;

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

		void update()
		{
			Gui::beginFrame();

			// Process events and open the event file if needed
			if (serializedEventFile == nullptr && (serializeEvents || playFromEventFile))
			{
				const std::string eventFilepath = World::getWorldEventFilepath(World::savePath);
				if (serializeEvents)
				{
					serializedEventFile = fopen(eventFilepath.c_str(), "wb");
				}
				else
				{
					serializedEventFile = fopen(eventFilepath.c_str(), "rb");
				}

				if (!serializedEventFile)
				{
					serializeEvents = false;
					playFromEventFile = false;
					g_logger_error("Could not open filepath '%s' to serialize world events.", eventFilepath.c_str());
					serializedEventFile = nullptr;
				}
			}

			if (serializedEventFile && (!serializeEvents && !playFromEventFile))
			{
				fclose(serializedEventFile);
				serializedEventFile = nullptr;
			}

			queueMainEvent(GEventType::SetDeltaTime, &Application::deltaTime, sizeof(float), false);
			processEvents();

			switch (currentScene)
			{
			case SceneType::SinglePlayerGame:
			case SceneType::LocalLanGame:
			case SceneType::MultiplayerGame:
				World::update(cameraFrustum, worldTexture);
				break;
			case SceneType::MainMenu:
				MainMenu::update();
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

			if (serializedEventFile)
			{
				fclose(serializedEventFile);
				serializedEventFile = nullptr;
				serializeEvents = false;
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

		void queueMainEvent(GEventType type, void* eventData, size_t eventDataSize, bool freeData)
		{
			if (!playFromEventFile)
			{
				GEvent newEvent;
				newEvent.type = type;
				newEvent.data = eventData;
				newEvent.size = eventDataSize;
				newEvent.freeData = freeData;
				events.emplace(newEvent);
			}
		}

		void queueMainEventKey(int key, int action)
		{
			if (!playFromEventFile)
			{
				GEvent newEvent;
				newEvent.type = GEventType::PlayerKeyInput;
				newEvent.data = g_memory_allocate(sizeof(int) * 2);
				g_memory_copyMem(newEvent.data, &key, sizeof(int));
				g_memory_copyMem((int*)newEvent.data + 1, &action, sizeof(int));
				newEvent.size = sizeof(int) * 2;
				newEvent.freeData = true;
				events.emplace(newEvent);
			}
		}

		void queueMainEventMouse(float xpos, float ypos)
		{
			if (!playFromEventFile)
			{
				GEvent newEvent;
				newEvent.type = GEventType::PlayerMouseInput;
				newEvent.data = g_memory_allocate(sizeof(float) * 2);
				g_memory_copyMem(newEvent.data, &xpos, sizeof(float));
				g_memory_copyMem((float*)newEvent.data + 1, &ypos, sizeof(float));
				newEvent.size = sizeof(float) * 2;
				newEvent.freeData = true;
				events.emplace(newEvent);
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
			registry->registerComponent<PlayerComponent>("PlayerComponent");
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

		static void processEvents()
		{
			if (!playFromEventFile)
			{
				// Do regular event loop
				while (events.size() > 0)
				{
					const GEvent& nextEvent = events.front();

					processEvent(nextEvent.type, nextEvent.data, nextEvent.size);

					if (serializeEvents)
					{
						serializeEvent(nextEvent);
					}

					if (nextEvent.freeData)
					{
						g_memory_free(nextEvent.data);
					}

					events.pop();
				}
			}
			else if (playFromEventFile && serializedEventFile != nullptr)
			{
				// Play events from the event file

				// A command should only ever be 16 bytes big
				uint8 buffer[16];
				GEventType eventType = GEventType::None;
				while (eventType != GEventType::SetDeltaTime && serializedEventFile && playFromEventFile)
				{
					int readResult = fread(buffer, sizeof(uint8), 1, serializedEventFile);
					if (readResult)
					{
						eventType = *(GEventType*)&buffer;
						size_t dataSize = getEventSize(eventType);
						readResult = fread(buffer, dataSize, 1, serializedEventFile);

						if (readResult)
						{
							processEvent(eventType, buffer, dataSize);
						}
					}

					if (!readResult)
					{
						g_logger_error("Failed to read event file for some reason. Stopping the replay.");
						fclose(serializedEventFile);
						serializedEventFile = nullptr;
						playFromEventFile = false;
						break;
					}
				}
			}
		}

		static void serializeEvent(const GEvent& event)
		{
			g_logger_assert(event.size <= 15,
				"Bad world event data for event '%s'. WorldEvents can only be a maximum of 15 bytes right now.",
				magic_enum::enum_name(event.type).data());

			// Write the event type
			fwrite(&event.type, sizeof(GEventType), 1, serializedEventFile);

			// Write data depending on event type
			switch (event.type)
			{
			case GEventType::SetDeltaTime:
			{
				fwrite(event.data, sizeof(float), 1, serializedEventFile);
				break;
			}
			case GEventType::PlayerKeyInput:
			{
				fwrite(event.data, sizeof(int) * 2, 1, serializedEventFile);
				break;
			}
			case GEventType::PlayerMouseInput:
			{
				fwrite(event.data, sizeof(float) * 2, 1, serializedEventFile);
				break;
			}
			case GEventType::SetPlayerPos:
			{
				fwrite(event.data, sizeof(glm::vec3), 1, serializedEventFile);
				break;
			}
			case GEventType::SetPlayerViewAxis:
			{
				fwrite(event.data, sizeof(glm::vec2), 1, serializedEventFile);
				break;
			}
			default:
				g_logger_error("Tried to serialize unknown event '%s' in World::getEventSize().", magic_enum::enum_name(event.type).data());
				break;
			}
		}

		static size_t getEventSize(GEventType type)
		{
			switch (type)
			{
			case GEventType::SetDeltaTime:
				return sizeof(float);
			case GEventType::PlayerKeyInput:
				return sizeof(int) * 2;
			case GEventType::PlayerMouseInput:
				return sizeof(float) * 2;
			case GEventType::SetPlayerPos:
				return sizeof(glm::vec3);
			case GEventType::SetPlayerViewAxis:
				return sizeof(glm::vec2);
			default:
				g_logger_error("Tried to get size of unknown event '%s' in World::getEventSize().", magic_enum::enum_name(type).data());
				break;
			}

			return 0;
		}

		static void processEvent(GEventType type, void* data, size_t sizeOfData)
		{
			g_logger_assert(sizeOfData <= 15,
				"Bad world event data for event '%s'. WorldEvents can only be a maximum of 15 bytes right now.",
				magic_enum::enum_name(type).data());

			switch (type)
			{
			case GEventType::SetDeltaTime:
			{
#ifdef _DEBUG
				g_logger_assert(sizeOfData == sizeof(float), "Expected sizeof(float) for SetDeltaTime event.");
#endif
				World::deltaTime = *(float*)data;
				break;
			}
			case GEventType::PlayerKeyInput:
			{
#ifdef _DEBUG
				g_logger_assert(sizeOfData == sizeof(int) * 2, "Expected sizeof(int) * 2 for PlayerKeyInput event.");
#endif
				int key = *(int*)data;
				int action = *(((int*)data) + 1);
				Input::processKeyEvent(key, action);
				break;
			}
			case GEventType::PlayerMouseInput:
			{
#ifdef _DEBUG
				g_logger_assert(sizeOfData == sizeof(float) * 2, "Expected sizeof(float) * 2 for PlayerKeyInput event.");
#endif
				float xpos = *(float*)data;
				float ypos = *(((float*)data) + 1);
				Input::processMouseEvent(xpos, ypos);
				break;
			}
			case GEventType::SetPlayerPos:
			{
#ifdef _DEBUG
				g_logger_assert(sizeOfData == sizeof(glm::vec3), "Expected sizeof(glm::vec3) for SetPlayerPos event.");
#endif
				glm::vec3* playerPos = (glm::vec3*)data;
				const Ecs::EntityId playerId = World::getLocalPlayer();
				Transform& playerTransform = registry->getComponent<Transform>(playerId);
				playerTransform.position = *playerPos;
				break;
			}
			case GEventType::SetPlayerViewAxis:
			{
#ifdef _DEBUG
				g_logger_assert(sizeOfData == sizeof(glm::vec2), "Expected sizeof(glm::vec2) for SetPlayerViewAxis event.");
#endif
				glm::vec2* viewAxis = (glm::vec2*)data;
				const Ecs::EntityId playerId = World::getLocalPlayer();
				CharacterController& controller = registry->getComponent<CharacterController>(playerId);
				controller.viewAxis = *viewAxis;
				break;
			}
			default:
				g_logger_error("Tried to process unknown event '%s' in World::processEvents().", magic_enum::enum_name(type).data());
				break;
			}
		}
	}
}