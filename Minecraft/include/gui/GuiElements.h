#ifndef MINECRAFT_GUI_ELEMENTS_H
#define MINECRAFT_GUI_ELEMENTS_H

namespace Minecraft
{
	struct TexturedButton;

	namespace GuiElements
	{
		extern TexturedButton* defaultButton;

		void init();

		void free();
	}
}

#endif 