#include "gui/Gui.h"
#include "input/Input.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "renderer/Font.h"

namespace Minecraft
{
	enum class WidgetState : uint8
	{
		None = 0,
		Hover,
		Click
	};

	namespace Gui
	{
		// Internal variables
		static Style guiStyle;

		// Internal functions
		static WidgetState mouseInAABB(const glm::vec2& position, const glm::vec2& size);

		void init()
		{
			guiStyle = Styles::defaultStyle;
		}

		bool button(const Button& button)
		{
			bool res = false;
			WidgetState state = mouseInAABB(button.position, button.size);
			if (state == WidgetState::Click)
			{
				guiStyle.color = button.clickColor;
				res = true;
			}
			else if (state == WidgetState::Hover)
			{
				guiStyle.color = button.hoverColor;
			}
			else
			{
				guiStyle.color = button.color;
			}

			Renderer::drawFilledSquare2D(button.position, button.size, guiStyle, -1);

			g_logger_assert(button.text != nullptr, "Invalid button text. Cannot be null.");
			g_logger_assert(button.font != nullptr, "Invalid button font. Cannot be null.");
			glm::vec2 strSize = button.font->getSize(button.text, button.textScale);
			glm::vec2 textPos = button.position + ((button.size - strSize) * 0.5f);
			Renderer::drawString(button.text, *button.font, textPos, button.textScale, guiStyle);

			return res;
		}

		bool textureButton(const TexturedButton& button)
		{
			const Sprite* sprite = nullptr;
			guiStyle.color = "#ffffff"_hex;

			bool res = false;
			WidgetState state = mouseInAABB(button.position, button.size);
			if (state == WidgetState::Click)
			{
				res = true;
				sprite = &button.clickSprite;
			}
			else if (state == WidgetState::Hover)
			{
				sprite = &button.hoverSprite;
			}
			else
			{
				sprite = &button.sprite;
			}

			Renderer::drawTexture2D(*sprite, button.position, button.size, guiStyle, -1);

			g_logger_assert(button.text != nullptr, "Invalid button text. Cannot be null.");
			g_logger_assert(button.font != nullptr, "Invalid button font. Cannot be null.");
			glm::vec2 strSize = button.font->getSize(button.text, button.textScale);
			glm::vec2 textPos = button.position + ((button.size - strSize) * 0.5f);
			Renderer::drawString(button.text, *button.font, textPos, button.textScale, guiStyle);

			return res;
		}

		bool slider(const Slider& slider, float* value)
		{
			static bool isDragging = false;

			const glm::vec2& sliderPosition = slider.position;
			const glm::vec2& sliderSize = slider.size;
			float normalizedValue = (*value - slider.minValue) / (slider.maxValue - slider.minValue);
			normalizedValue = glm::clamp(normalizedValue, 0.0f, 1.0f);

			const glm::vec2& buttonSize = glm::vec2(0.05f, 0.1f);
			float offsetY = (sliderSize.y - buttonSize.y) * 0.5f;
			const glm::vec2& buttonPosition = sliderPosition + glm::vec2(normalizedValue * sliderSize.x, offsetY);

			guiStyle.color = "#1a1a1a"_hex;
			Renderer::drawFilledSquare2D(sliderPosition, sliderSize, guiStyle, -1);
			WidgetState state = mouseInAABB(buttonPosition, buttonSize);
			if (state == WidgetState::Click)
			{
				isDragging = true;
			}
			else if (state == WidgetState::Hover)
			{
				guiStyle.color = "#636363"_hex;
				Renderer::drawFilledSquare2D(buttonPosition, buttonSize, guiStyle);
			}
			else
			{
				// We are not hovering
				guiStyle.color = "#e3e3e3"_hex;
				Renderer::drawFilledSquare2D(buttonPosition, buttonSize, guiStyle);
			}

			if (isDragging)
			{
				if (!Input::isMousePressed(GLFW_MOUSE_BUTTON_LEFT))
				{
					isDragging = false;
				}

				// Set the value to the position of the mouse
				float normalizedMouseX = (Input::mouseScreenX - sliderPosition.x) / sliderSize.x;
				*value = (normalizedMouseX * (slider.maxValue - slider.minValue)) + slider.minValue;
				*value = glm::clamp(*value, slider.minValue, slider.maxValue);

				// We are clicking the button 
				guiStyle.color = "#ababab"_hex;
				Renderer::drawFilledSquare2D(buttonPosition, buttonSize, guiStyle);
				return true;
			}

			return false;
		}

		static WidgetState mouseInAABB(const glm::vec2& position, const glm::vec2& size)
		{
			if (Input::mouseScreenX >= position.x && Input::mouseScreenX <= position.x + size.x &&
				Input::mouseScreenY >= position.y && Input::mouseScreenY <= position.y + size.y)
			{
				// We are hovering over the button
				if (Input::isMousePressed(GLFW_MOUSE_BUTTON_LEFT))
				{
					// We are clicking the button 
					return WidgetState::Click;
				}
				else
				{
					return WidgetState::Hover;
				}
			}

			return WidgetState::None;
		}
	}
}