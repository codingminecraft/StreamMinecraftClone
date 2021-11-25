#include "renderer/Styles.h"

namespace Minecraft
{
	namespace Colors
	{
		glm::vec4 greenBrown = "#272822FF"_hex;
		glm::vec4 offWhite = "#F8F8F2FF"_hex;
		glm::vec4 darkGray = "#75715EFF"_hex;
		glm::vec4 red = "#F92672FF"_hex;
		glm::vec4 orange = "#FD971FFF"_hex;
		glm::vec4 lightOrange = "#E69F66FF"_hex;
		glm::vec4 yellow = "#E6DB74FF"_hex;
		glm::vec4 green = "#A6E22EFF"_hex;
		glm::vec4 blue = "#66D9EFFF"_hex;
		glm::vec4 purple = "#AE81FFFF"_hex;
	}

	namespace Styles
	{
		Style defaultStyle = {
			Colors::offWhite,
			0.05f,
			CapType::Flat
		};
	}
}