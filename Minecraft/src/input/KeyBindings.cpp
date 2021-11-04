#include "input/KeyBindings.h"
#include "input/Input.h"

namespace Minecraft
{
	namespace KeyBindings
	{
		static robin_hood::unordered_map<KeyBind, uint32> bindings;

		void init()
		{
			bindings = robin_hood::unordered_map<KeyBind, uint32>();

			// TODO: Load this from a key-binding config file
			bindings[KeyBind::LockCursor] = GLFW_KEY_F2;
			bindings[KeyBind::ShowHideDebugStats] = GLFW_KEY_F3;
			bindings[KeyBind::Exit] = GLFW_KEY_F10;
			bindings[KeyBind::ShowCommandLine] = GLFW_KEY_SLASH;
			bindings[KeyBind::ShowChat] = GLFW_KEY_T;
			bindings[KeyBind::Escape] = GLFW_KEY_ESCAPE;
			bindings[KeyBind::Enter] = GLFW_KEY_ENTER;
		}

		void setKeyBinding(KeyBind key, uint32 value)
		{
			bindings[key] = value;
		}

		uint32 getKeyBinding(KeyBind key)
		{
			if (bindings.find(key) != bindings.end())
			{
				return bindings[key];
			}

			g_logger_warning("Unable to find KeyBindings key: %d", key);
			return GLFW_KEY_LAST;
		}

		bool keyBeginPress(KeyBind key)
		{
			return Input::keyBeginPress(getKeyBinding(key));
		}
	}
}