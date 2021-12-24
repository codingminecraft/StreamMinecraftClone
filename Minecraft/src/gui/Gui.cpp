#include "gui/Gui.h"
#include "input/Input.h"
#include "renderer/Renderer.h"
#include "renderer/Styles.h"
#include "renderer/Font.h"
#include "core/Application.h"

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
		glm::vec2 lastElementPosition;
		glm::vec2 lastElementSize;
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
		static float defaultTextScale;
		static const float keyHoldDelayTime = 0.05f;

		// Internal functions
		static WidgetState mouseInAABB(const glm::vec2& position, const glm::vec2& size);
		static void pushWindow(const WindowState& frameState);
		static void popWindow();
		static WindowState& getCurrentWindow();
		static void advanceCursorPastElement(WindowState& window, const glm::vec2& elementSize);
		static glm::vec2 getElementPosition(WindowState& window, const glm::vec2& elementSize);
		static bool elementExceedsWindowHeight(WindowState& window, const glm::vec2& elementSize);

		void init()
		{
			guiStyle = Styles::defaultStyle;
			currentWindow = -1;
			elementPadding = glm::vec2(0.01f, 0.01f);
			defaultFont = Fonts::loadFont("assets/fonts/Minecraft.ttf", 16_px);
			defaultTextScale = 0.0025f;
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
			if (elementExceedsWindowHeight(windowState, size))
			{
				return;
			}

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

		bool input(const char* text, float scale, char* inputBuffer, int inputBufferLength, bool drawOutline, bool isFocused, int zIndex)
		{
			// TODO: Add window overrun support (scroll support)
			WindowState& windowState = getCurrentWindow();
			sameLine();
			const float textBoxPadding = 0.02f;
			float height = (defaultFont->lineHeight * scale) + (textBoxPadding * 2.0f);
			label(text, scale, height);

			float windowWidthLeft = windowState.size.x - windowState.cursorPos.x;
			glm::vec2 inputBoxSize = glm::vec2(windowWidthLeft, height);
			glm::vec2 inputBoxPos = getElementPosition(windowState, inputBoxSize);
			WidgetState state = mouseInAABB(inputBoxPos, inputBoxSize);
			static bool focused = false || isFocused;
			if (state == WidgetState::Hover)
			{
				focused = true;
				guiStyle.color = "#00000099"_hex;
			}
			else
			{
				guiStyle.color = "#00000066"_hex;
			}
			Renderer::drawFilledSquare2D(inputBoxPos, inputBoxSize, guiStyle, zIndex - 1);

			guiStyle.color = "#ffffff"_hex;
			if (drawOutline)
			{
				guiStyle.strokeWidth = 0.01f;
				Renderer::drawSquare2D(inputBoxPos, inputBoxSize, guiStyle, zIndex - 2);
			}

			std::string inputText = std::string(inputBuffer);
			glm::vec2 inputTextStrSize = defaultFont->getSize(inputText, scale);
			glm::vec2 sizeOfPipeChar = defaultFont->getSize("|", scale);
			if (inputTextStrSize.x >= (inputBoxSize.x - sizeOfPipeChar.x - textBoxPadding))
			{
				// Figure out how many chars fit in the line
				inputText = defaultFont->getStringThatFitsIn(inputText, scale, inputBoxSize.x - sizeOfPipeChar.x - textBoxPadding, false);
				inputTextStrSize = defaultFont->getSize(inputText, scale);
			}
			float inputCursorPosX = textBoxPadding;
			if (inputText.length() > 0)
			{
				// TODO: Simplify this, there has to be a better way to center this stuff
				RenderableChar charInfo = defaultFont->getCharInfo('|');
				float heightOfPipe = charInfo.charSize.y * scale;
				float centeredHeight = (height - heightOfPipe) / 2.0f;
				Renderer::drawString(inputText, *defaultFont, inputBoxPos + glm::vec2(inputCursorPosX, centeredHeight + ((charInfo.charSize.y - charInfo.bearingY) * scale) / 2.0f), scale, guiStyle, zIndex);
				Style style = Styles::defaultStyle;
				style.strokeWidth = 0.01f;
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
					// TODO: Simplify this, there has to be a better way to center this stuff
					RenderableChar charInfo = defaultFont->getCharInfo('|');
					float heightOfPipe = charInfo.charSize.y * scale;
					float centeredHeight = (height - heightOfPipe) / 2.0f;
					Renderer::drawString("|", *defaultFont, inputBoxPos + glm::vec2(inputCursorPosX, centeredHeight + (charInfo.charSize.y - charInfo.bearingY) * scale), scale, guiStyle, zIndex);
				}

				uint32 c = Input::lastCharPressed();
				if (c != '\0')
				{
					bool placedNullChar = false;
					for (int i = 0; i < inputBufferLength - 1; i++)
					{
						if (inputBuffer[i] == '\0')
						{
							inputBuffer[i] = c;
							inputBuffer[i + 1] = '\0';
							placedNullChar = true;
							break;
						}
					}

					res = placedNullChar;
					if (!placedNullChar)
					{
						g_logger_error("Ran out of room in input buffer, terminating string early.");
						inputBuffer[inputBufferLength - 1] = '\0';
					}
				}

				static float backspaceDelayTimeLeft = 0.0f;
				static bool backspacePressedLastFrame = false;
				if (Input::isKeyPressed(GLFW_KEY_BACKSPACE))
				{
					if (backspaceDelayTimeLeft <= 0.0f)
					{
						backspaceDelayTimeLeft = keyHoldDelayTime * (backspacePressedLastFrame ? 1.0f : 6.0f);
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
						backspacePressedLastFrame = true;
					}
					res = true;
				}
				else
				{
					backspacePressedLastFrame = false;
				}
				backspaceDelayTimeLeft -= Application::deltaTime;


			}

			Gui::advanceCursorPastElement(windowState, inputBoxSize);
			return res;
		}

		bool button(const Button& button)
		{
			// TODO: Add window overrun support (scroll support)
			WindowState& windowState = getCurrentWindow();
			if (elementExceedsWindowHeight(windowState, button.size))
			{
				return false;
			}

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

		bool textureButton(const TexturedButton& button, bool isDisabled)
		{
			WindowState& windowState = getCurrentWindow();
			if (elementExceedsWindowHeight(windowState, button.size))
			{
				return false;
			}

			const Sprite* sprite = nullptr;
			guiStyle.color = "#ffffff"_hex;

			bool res = false;
			glm::vec2 buttonPosition = getElementPosition(windowState, button.size);
			WidgetState state = mouseInAABB(buttonPosition, button.size);
			if (state == WidgetState::Click || isDisabled)
			{
				// If the button isn't disabled and they clicked it, then set the result to true
				// Otherwise if it's disabled set the result to false
				res = !isDisabled;
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

		bool worldSaveItem(const char* worldDataPath, const glm::vec2& size, const Sprite& icon, bool isSelected)
		{
			WindowState& windowState = getCurrentWindow();
			if (elementExceedsWindowHeight(windowState, size))
			{
				return false;
			}

			guiStyle.color = "#ffffff"_hex;

			bool res = false;
			glm::vec2 buttonPosition = getElementPosition(windowState, size);
			WidgetState state = mouseInAABB(buttonPosition, size);
			if (state == WidgetState::Click)
			{
				res = true;
			}

			glm::vec2 imageSize = glm::vec2(size.y - elementPadding.y * 2.0f, size.y - elementPadding.y * 2.0f);
			glm::vec2 imagePos = buttonPosition + elementPadding;
			if (!icon.texture.isNull())
			{
				Renderer::drawTexture2D(icon, imagePos, imageSize, guiStyle, 0);
			}
			else
			{
				Renderer::drawFilledSquare2D(imagePos, imageSize, Styles::defaultStyle);
			}

			g_logger_assert(worldDataPath != nullptr, "Invalid world data path. Cannot be null.");
			g_logger_assert(defaultFont != nullptr, "Invalid default font. Cannot be null.");
			glm::vec2 strSize = defaultFont->getSize(worldDataPath, defaultTextScale);
			glm::vec2 textPos = imagePos + imageSize + glm::vec2(elementPadding.x, -(size.y - (strSize.y * 0.5f)));
			std::string sanitizedWorldDataPath = std::string(worldDataPath);
			float maxSize = size.x - defaultFont->getSize("...", defaultTextScale).x - imageSize.x - (elementPadding.x * 2.0f);
			if (strSize.x >= maxSize)
			{
				sanitizedWorldDataPath = defaultFont->getStringThatFitsIn(sanitizedWorldDataPath, defaultTextScale, maxSize) + "...";
			}
			Renderer::drawString(sanitizedWorldDataPath, *defaultFont, textPos, defaultTextScale, guiStyle);


			if (isSelected)
			{
				static Style lineStyle = Styles::defaultStyle;
				lineStyle.strokeWidth = 0.01f;
				Renderer::drawSquare2D(buttonPosition, size, lineStyle);
			}

			advanceCursorPastElement(windowState, size);
			return res;
		}

		bool slider(const Slider& slider, float* value)
		{
			WindowState& windowState = getCurrentWindow();
			if (elementExceedsWindowHeight(windowState, slider.size))
			{
				return false;
			}

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

		glm::vec2 getLastElementSize()
		{
			return windows[currentWindow].lastElementSize;
		}

		glm::vec2 getLastElementPosition()
		{
			return windows[currentWindow].position +
				glm::vec2(windows[currentWindow].lastElementPosition.x,
					windows[currentWindow].lastElementPosition.y - windows[currentWindow].lastElementSize.y);
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
				if (Input::mouseBeginPress(GLFW_MOUSE_BUTTON_LEFT))
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
			window.lastElementPosition = window.cursorPos - elementPadding;
			window.lastElementSize = elementSize + elementPadding;
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

		static bool elementExceedsWindowHeight(WindowState& window, const glm::vec2& elementSize)
		{
			window.lastElementPosition = window.cursorPos - elementPadding;
			window.lastElementSize = elementSize + elementPadding;

			glm::vec2 nextPos;
			if (window.nextElementSameLine)
			{
				nextPos = window.cursorPos + glm::vec2(elementSize.x, 0) + glm::vec2(elementPadding.x, 0);

			}
			else
			{
				nextPos = glm::vec2(0, window.cursorPos.y) + elementPadding + elementSize.y;
			}

			return false;// nextPos.x >= (window.size.x + window.cursorPos.x) ||
				//nextPos.y >= (window.size.y + window.cursorPos.y) ||
				//nextPos.x < window.cursorPos.x ||
				//nextPos.y < window.cursorPos.y;
		}
	}
}