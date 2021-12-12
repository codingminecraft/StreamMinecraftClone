#ifndef MINECRAFT_GUI_H
#define MINECRAFT_GUI_H
#include "core.h"
#include "renderer/Sprites.h"

namespace Minecraft
{
	struct Texture;
	struct Font;

	struct Button
	{
		glm::vec2 size;
		glm::vec4 color;
		glm::vec4 hoverColor;
		glm::vec4 clickColor;
		const char* text;
		float textScale;
		Font* font;
	};

	struct TexturedButton
	{
		glm::vec2 size;
		Sprite sprite;
		Sprite hoverSprite;
		Sprite clickSprite;
		const char* text;
		float textScale;
		Font* font;
	};

	struct Slider
	{
		glm::vec2 size;
		float minValue;
		float maxValue;
	};

	namespace Gui
	{
		void init();

		void beginFrame();
		void beginWindow(const glm::vec2& position, const glm::vec2& size, int numColumns = 0);
		void endWindow();
		void advanceCursor(const glm::vec2& delta);

		void centerNextElement();
		void sameLine();
		void image(const Sprite& sprite, const glm::vec2& size);
		void label(const char* text, float scale, float maxHeight = -1.0f);

		bool input(const char* text, float scale, char* inputBuffer, int inputBufferLength, bool drawOutline = false, bool isFocused = false, int zIndex = 0);
		bool button(const Button& button);
		bool textureButton(const TexturedButton& button, bool isDisabled = false);
		bool worldSaveItem(const char* worldDataPath, const glm::vec2& size, const Sprite& icon, bool isSelected);
		
		bool slider(const Slider& slider, float* value);

		glm::vec2 getLastElementSize();
		glm::vec2 getLastElementPosition();
	}
}

#endif 