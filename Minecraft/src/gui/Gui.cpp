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

	struct WindowState
	{
		// Frame starts at top-left, and goes down-right according to gui element sizes
		glm::vec2 cursorPos;
		glm::vec2 position;
		glm::vec2 size;
		int numColumns;
		bool nextElementSameLine;
		bool centerNextElement;
		bool fillNextElement;
	};

	namespace Gui
	{
		// Internal variables
		// Change max if ever needed
		static std::array<WindowState, 10> windows;
		static int currentWindow;
		static Style guiStyle;
		static glm::vec2 elementPadding;
		static Font* defaultFont;

		// Internal functions
		static WidgetState mouseInAABB(const glm::vec2& position, const glm::vec2& size);
		static void pushWindow(const WindowState& frameState);
		static void popWindow();
		static WindowState& getCurrentWindow();
		static void advanceCursorPastElement(WindowState& window, const glm::vec2& elementSize);
		static glm::vec2 getElementPosition(WindowState& window, const glm::vec2& elementSize);

		void init()
		{
			guiStyle = Styles::defaultStyle;
			currentWindow = -1;
			elementPadding = glm::vec2(0.01f, 0.01f);
			defaultFont = Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);
		}

		void beginFrame()
		{
			g_logger_assert(currentWindow == -1, "Invalid current window. Did you forget to pop a window?");
		}

		void beginWindow(const glm::vec2& position, const glm::vec2& size, int numColumns)
		{
			WindowState state;
			state.cursorPos = elementPadding;
			state.numColumns = numColumns;
			state.size = size;
			state.position = position;
			state.centerNextElement = false;
			state.nextElementSameLine = false;
			pushWindow(state);
		}

		void endWindow()
		{
			popWindow();
		}

		void advanceCursor(const glm::vec2& delta)
		{
			WindowState& window = getCurrentWindow();
			window.cursorPos += glm::vec2(delta.x, -delta.y);
		}

		void centerNextElement()
		{
			WindowState& windowState = getCurrentWindow();
			windowState.centerNextElement = true;
		}

		void sameLine()
		{
			WindowState& windowState = getCurrentWindow();
			windowState.nextElementSameLine = true;
		}

		void image(const Sprite& sprite, const glm::vec2& size)
		{
			WindowState& windowState = getCurrentWindow();
			glm::vec2 spritePosition = getElementPosition(windowState, size);
			Renderer::drawTexture2D(sprite, spritePosition, size, Styles::defaultStyle);
			advanceCursorPastElement(windowState, size);
		}

		void label(const char* text, float scale, float maxHeight)
		{
			WindowState& windowState = getCurrentWindow();
			g_logger_assert(defaultFont != nullptr, "Default font was invalid.");
			glm::vec2 strSize = defaultFont->getSize(text, scale);
			if (maxHeight != -1.0f && maxHeight > strSize.y)
			{
				strSize.y += (maxHeight - strSize.y) / 2.0f;
			}
			glm::vec2 textPos = getElementPosition(windowState, strSize);
			guiStyle.color = "#ffffff"_hex;
			Renderer::drawString(text, *defaultFont, textPos, scale, guiStyle);
			advanceCursorPastElement(windowState, strSize);
		}

		bool input(const char* text, float scale, char* inputBuffer, int inputBufferLength)
		{
			WindowState& windowState = getCurrentWindow();
			sameLine();
			const float textBoxPadding = 0.02f;
			float height = defaultFont->getSize("M").y + textBoxPadding;
			label(text, scale, height);

			float windowWidthLeft = windowState.size.x - windowState.cursorPos.x;
			glm::vec2 inputBoxSize = glm::vec2(windowWidthLeft, height);
			glm::vec2 inputBoxPos = getElementPosition(windowState, inputBoxSize);
			WidgetState state = mouseInAABB(inputBoxPos, inputBoxSize);
			static bool focused = false;
			if (state == WidgetState::Hover)
			{
				focused = true;
				guiStyle.color = "#00000099"_hex;
			}
			else
			{
				guiStyle.color = "#00000066"_hex;
			}
			Renderer::drawFilledSquare2D(inputBoxPos, inputBoxSize, guiStyle, -1);

			guiStyle.color = "#ffffff"_hex;
			std::string inputText = std::string(inputBuffer);
			glm::vec2 inputTextStrSize = defaultFont->getSize(inputText, scale);
			float inputCursorPosX = textBoxPadding / 2.0f;
			if (inputText.length() > 0)
			{
				float centeredHeight = (height - inputTextStrSize.y - textBoxPadding) / 2.0f;
				Renderer::drawString(inputText, *defaultFont, inputBoxPos + glm::vec2(inputCursorPosX, centeredHeight), scale, guiStyle);
				inputCursorPosX += inputTextStrSize.x + 0.01f;
			}

			bool res = false;
			if (focused)
			{
				static const int maxCursorBlink = 50;
				static int cursorBlinkTick = 0;
				cursorBlinkTick = (cursorBlinkTick + 1) % maxCursorBlink;
				if (cursorBlinkTick > maxCursorBlink / 2)
				{
					float centeredHeight = -textBoxPadding / 2.0f;
					Renderer::drawString("|", *defaultFont, inputBoxPos + glm::vec2(inputCursorPosX, centeredHeight), scale, guiStyle);
				}

				uint32 c = Input::lastCharPressed();
				if (c != '\0')
				{
					for (int i = 0; i < inputBufferLength - 1; i++)
					{
						if (inputBuffer[i] == '\0')
						{
							inputBuffer[i] = c;
							inputBuffer[i + 1] = '\0';
							break;
						}
					}
					res = true;
				}
				if (Input::keyBeginPress(GLFW_KEY_BACKSPACE))
				{
					for (int i = 0; i < inputBufferLength; i++)
					{
						if (inputBuffer[i] == '\0')
						{
							if (i > 0)
							{
								inputBuffer[i - 1] = '\0';
							}
							break;
						}
					}
					res = true;
				}
			}

			Gui::advanceCursorPastElement(windowState, inputBoxSize);
			return res;
		}

		bool button(const Button& button)
		{
			WindowState& windowState = getCurrentWindow();

			bool res = false;
			glm::vec2 buttonPosition = getElementPosition(windowState, button.size);
			WidgetState state = mouseInAABB(buttonPosition, button.size);
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

			Renderer::drawFilledSquare2D(buttonPosition, button.size, guiStyle, -1);

			g_logger_assert(button.text != nullptr, "Invalid button text. Cannot be null.");
			g_logger_assert(button.font != nullptr, "Invalid button font. Cannot be null.");
			glm::vec2 strSize = button.font->getSize(button.text, button.textScale);
			glm::vec2 textPos = buttonPosition + ((button.size - strSize) * 0.5f);
			Renderer::drawString(button.text, *button.font, textPos, button.textScale, guiStyle);

			advanceCursorPastElement(windowState, button.size);
			return res;
		}

		bool textureButton(const TexturedButton& button)
		{
			WindowState& windowState = getCurrentWindow();

			const Sprite* sprite = nullptr;
			guiStyle.color = "#ffffff"_hex;

			bool res = false;
			glm::vec2 buttonPosition = getElementPosition(windowState, button.size);
			WidgetState state = mouseInAABB(buttonPosition, button.size);
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

			Renderer::drawTexture2D(*sprite, buttonPosition, button.size, guiStyle, -1);

			g_logger_assert(button.text != nullptr, "Invalid button text. Cannot be null.");
			g_logger_assert(button.font != nullptr, "Invalid button font. Cannot be null.");
			glm::vec2 strSize = button.font->getSize(button.text, button.textScale);
			glm::vec2 textPos = buttonPosition + ((button.size - strSize) * 0.5f);
			Renderer::drawString(button.text, *button.font, textPos, button.textScale, guiStyle);

			advanceCursorPastElement(windowState, button.size);
			return res;
		}

		bool slider(const Slider& slider, float* value)
		{
			WindowState& windowState = getCurrentWindow();

			const glm::vec2& sliderPosition = getElementPosition(windowState, slider.size);
			const glm::vec2& sliderSize = slider.size;
			float normalizedValue = (*value - slider.minValue) / (slider.maxValue - slider.minValue);
			normalizedValue = glm::clamp(normalizedValue, 0.0f, 1.0f);

			const glm::vec2& buttonSize = glm::vec2(0.05f, 0.1f);
			float offsetY = (sliderSize.y - buttonSize.y) * 0.5f;
			const glm::vec2& buttonPosition = sliderPosition + glm::vec2(normalizedValue * sliderSize.x, offsetY);

			guiStyle.color = "#1a1a1a"_hex;
			Renderer::drawFilledSquare2D(sliderPosition, sliderSize, guiStyle, -1);

			static bool isDragging = false;
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

			advanceCursorPastElement(windowState, sliderSize);
			return false;
		}

		// ===================================
		// Internal Functions
		// ===================================
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

		static void pushWindow(const WindowState& frameState)
		{
			currentWindow++;
			g_logger_assert(currentWindow >= 0 && currentWindow < windows.size(), "Window index out of bounds %d", currentWindow);
			windows[currentWindow] = frameState;
			windows[currentWindow].cursorPos = elementPadding;
		}

		static void popWindow()
		{
			currentWindow--;
			g_logger_assert(currentWindow >= -1, "Too many endWindow() calls.");
		}

		static WindowState& getCurrentWindow()
		{
			g_logger_assert(currentWindow > -1, "No windows are active. Did you miss a beginWindow() call?");
			g_logger_assert(currentWindow < windows.size(), "Too many windows.");

			return windows[currentWindow];
		}

		static void advanceCursorPastElement(WindowState& window, const glm::vec2& elementSize)
		{
			if (window.nextElementSameLine)
			{
				window.cursorPos += glm::vec2(elementSize.x, 0) + glm::vec2(elementPadding.x, 0);
				window.nextElementSameLine = false;
			}
			else
			{
				window.cursorPos = glm::vec2(0, window.cursorPos.y) + elementPadding - glm::vec2(0, elementSize.y);
			}
		}

		static glm::vec2 getElementPosition(WindowState& window, const glm::vec2& elementSize)
		{
			if (window.centerNextElement)
			{
				float windowWidthLeft = window.size.x - window.cursorPos.x;
				window.cursorPos.x += (windowWidthLeft / 2.0f) - (elementSize.x / 2.0f);
				window.centerNextElement = false;
				return window.cursorPos + window.position - glm::vec2(0, elementSize.y);
			}

			return window.cursorPos + window.position - glm::vec2(0, elementSize.y);
		}
	}
}