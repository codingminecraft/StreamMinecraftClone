#include "renderer/Styles.h"

namespace Minecraft
{
	namespace Colors
	{
		extern glm::vec4 greenBrown = "#272822FF"_hex;
		extern glm::vec4 offWhite = "#F8F8F2FF"_hex;
		extern glm::vec4 darkGray = "#75715EFF"_hex;
		extern glm::vec4 red = "#F92672FF"_hex;
		extern glm::vec4 orange = "#FD971FFF"_hex;
		extern glm::vec4 lightOrange = "#E69F66FF"_hex;
		extern glm::vec4 yellow = "#E6DB74FF"_hex;
		extern glm::vec4 green = "#A6E22EFF"_hex;
		extern glm::vec4 blue = "#66D9EFFF"_hex;
		extern glm::vec4 purple = "#AE81FFFF"_hex;
	}

	namespace Styles
	{
		extern Style defaultStyle = {
			Colors::offWhite,
			0.05f,
			CapType::Flat
		};
	}
}