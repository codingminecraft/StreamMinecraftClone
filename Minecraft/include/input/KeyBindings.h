#ifndef MINECRAFT_KEY_BINDINGS_H
#define MINECRAFT_KEY_BINDINGS_H
#include "core.h"

namespace Minecraft
{
	enum class KeyBind
	{
		ShowHideDebugStats,
		LockCursor,
		Exit,
		ShowChat,
		ShowCommandLine,
		Escape,
		Enter,
		Length
	};

	namespace KeyBindings
	{
		void init();
		void setKeyBinding(KeyBind key, uint32 value);
		uint32 getKeyBinding(KeyBind key);

		bool keyBeginPress(KeyBind key);
	}
}

#endif