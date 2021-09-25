#include "input/KeyHandler.h"
#include "input/Input.h"
#include "input/KeyBindings.h"
#include "core/Application.h"
#include "core/Window.h"
#include "utils/DebugStats.h"
#include "gameplay/CommandLine.h"

namespace Minecraft
{
	namespace KeyHandler
	{
		void update(float dt)
		{
			static bool showDebugStats = false;
			if (showDebugStats)
			{
				DebugStats::render();
			}

			static bool showCommandLine = false;
			if (showCommandLine)
			{
				CommandLine::update(dt);
			}

			if (KeyBindings::keyBeginPress(KeyBind::LockCursor))
			{
				static bool lockCursor = false;
				lockCursor = !lockCursor;
				Application::getWindow().setCursorMode(lockCursor ? CursorMode::Locked : CursorMode::Normal);
			}
			if (KeyBindings::keyBeginPress(KeyBind::ShowHideDebugStats))
			{
				showDebugStats = !showDebugStats;
			}
			if (KeyBindings::keyBeginPress(KeyBind::Exit))
			{
				Application::getWindow().close();
			}
			if (KeyBindings::keyBeginPress(KeyBind::ShowChat) || KeyBindings::keyBeginPress(KeyBind::ShowCommandLine))
			{
				showCommandLine = true;
			}
			if (KeyBindings::keyBeginPress(KeyBind::Escape))
			{
				showCommandLine = false;
			}
		}
	}
}