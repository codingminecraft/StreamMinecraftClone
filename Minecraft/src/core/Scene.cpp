#include "core/Scene.h"
#include "core/Components.h"
#include "world/World.h"
#include "gui/MainMenu.h"
#include "gui/Gui.h"
#include "renderer/Renderer.h"
#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "utils/Settings.h"
#include "input/Input.h"

namespace Minecraft
{
	namespace Scene
	{
		// Internal variables
		static SceneType currentScene = SceneType::None;
		static SceneType nextSceneType = SceneType::None;
		static bool changeSceneAtFrameEnd = false;
		static Ecs::Registry* registry;

		// TODO: Where does this go??
		static Camera camera;
		static Frustum cameraFrustum;

		// Internal functions
		static void changeSceneInternal();

		void init(SceneType type, Ecs::Registry& inRegistry)
		{
			registry = &inRegistry;

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

			changeScene(type);
		}

		void update(float dt)
		{
			Renderer::clearColor(Settings::Window::clearColor);
			Gui::beginFrame();

			switch (currentScene)
			{
			case SceneType::Game:
				World::update(dt, cameraFrustum);
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

		void free()
		{
			switch (currentScene)
			{
			case SceneType::Game:
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

		Camera& getCamera()
		{
			return camera;
		}

		static void changeSceneInternal()
		{
			free();

			switch (nextSceneType)
			{
			case SceneType::Game:
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
	}
}