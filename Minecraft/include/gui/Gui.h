#ifndef MINECRAFT_GUI_H
#define MINECRAFT_GUI_H
#include "core.h"

namespace Minecraft
{
	struct Texture;
	struct Font;

	struct Sprite
	{
		Texture* texture;
		glm::vec2 uvStart;
		glm::vec2 uvSize;
	};

	struct Button
	{
		glm::vec2 position;
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
		glm::vec2 position;
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
		glm::vec2 position;
		glm::vec2 size;
		float minValue;
		float maxValue;
	};

	namespace Gui
	{
		bool button(const Button& button);
		bool textureButton(const TexturedButton& button);
		
		bool slider(const Slider& slider, float* value);
	}
}

#endif 