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
		bool Compare(float x, float y, float epsilon = std::numeric_limits<float>::min());
		bool Compare(const glm::vec3& vec1, const glm::vec3& vec2, float epsilon = std::numeric_limits<float>::min());
		bool Compare(const glm::vec2& vec1, const glm::vec2& vec2, float epsilon = std::numeric_limits<float>::min());
		bool Compare(const glm::vec4& vec1, const glm::vec4& vec2, float epsilon = std::numeric_limits<float>::min());

		// Vector conversions
		glm::vec2 Vector2From3(const glm::vec3& vec);
		glm::vec3 Vector3From2(const glm::vec2& vec);

		// Math functions
		void Rotate(glm::vec2& vec, float angleDeg, const glm::vec2& origin);
		void Rotate(glm::vec3& vec, float angleDeg, const glm::vec3& origin);

		float ToRadians(float degrees);
		float ToDegrees(float radians);

		// Map Ranges
		float MapRange(float val, float inMin, float inMax, float outMin, float outMax);

		// Max, Min impls
		int Max(int a, int b);
		int Min(int a, int b);
		float Saturate(float val);

		// Hash Strings
		uint32 HashString(const char* str);
	}
}

#endif
