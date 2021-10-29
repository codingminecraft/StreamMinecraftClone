#ifndef MINECRAFT_C_MATH_H
#define MINECRAFT_C_MATH_H

#include "core.h"

// TODO: Find who's leaking min, max macros...
#ifdef min 
#undef min
#endif
#ifdef max
#undef max
#endif

namespace Minecraft
{
	namespace CMath
	{
		// Float Comparison functions, using custom epsilon
		bool compare(float x, float y, float epsilon = std::numeric_limits<float>::min());
		bool compare(const glm::vec3& vec1, const glm::vec3& vec2, float epsilon = std::numeric_limits<float>::min());
		bool compare(const glm::vec2& vec1, const glm::vec2& vec2, float epsilon = std::numeric_limits<float>::min());
		bool compare(const glm::vec4& vec1, const glm::vec4& vec2, float epsilon = std::numeric_limits<float>::min());

		// Vector conversions
		glm::vec2 vector2From3(const glm::vec3& vec);
		glm::vec3 vector3From2(const glm::vec2& vec);

		// Math functions
		void rotate(glm::vec2& vec, float angleDeg, const glm::vec2& origin);
		void rotate(glm::vec3& vec, float angleDeg, const glm::vec3& origin);

		float toRadians(float degrees);
		float toDegrees(float radians);

		// Map Ranges
		float mapRange(float val, float inMin, float inMax, float outMin, float outMax);

		// Max, Min impls
		int max(int a, int b);
		int min(int a, int b);
		float saturate(float val);

		// Hash Strings
		uint32 hashString(const char* str);

		// To String stuff
		std::string toString(const glm::vec4& vec4, int precision = 2);
		std::string toString(const glm::vec3& vec3, int precision = 2);
		std::string toString(const glm::vec2& vec2, int precision = 2);

		std::string toString(const glm::ivec4& vec4);
		std::string toString(const glm::ivec3& vec3);
		std::string toString(const glm::ivec2& vec2);

		std::string toString(float value, int precision = 2);

		int length2(const glm::ivec2& vec);
		int length2(const glm::ivec3& vec);
		int length2(const glm::ivec4& vec);

		int negativeMod(int value, int lowerBound, int upperBound);
	}
}

#endif
