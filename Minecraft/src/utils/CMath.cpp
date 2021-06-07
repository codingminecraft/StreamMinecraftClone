#include "utils/CMath.h"

namespace Minecraft
{
	namespace CMath
	{
		static float PI = 3.1415926535897932384626433832795028841971693993751058209749445923078164062f;

		bool Compare(float x, float y, float epsilon)
		{
			return abs(x - y) <= epsilon * std::max(1.0f, std::max(abs(x), abs(y)));
		}

		bool Compare(const glm::vec2& vec1, const glm::vec2& vec2, float epsilon)
		{
			return Compare(vec1.x, vec2.x, epsilon) && Compare(vec1.y, vec2.y, epsilon);
		}

		bool Compare(const glm::vec3& vec1, const glm::vec3& vec2, float epsilon)
		{
			return Compare(vec1.x, vec2.x, epsilon) && Compare(vec1.y, vec2.y, epsilon) && Compare(vec1.z, vec2.z, epsilon);
		}

		bool Compare(const glm::vec4& vec1, const glm::vec4& vec2, float epsilon)
		{
			return Compare(vec1.x, vec2.x, epsilon) && Compare(vec1.y, vec2.y, epsilon) && Compare(vec1.z, vec2.z, epsilon) && Compare(vec1.w, vec2.w, epsilon);
		}

		glm::vec2 Vector2From3(const glm::vec3& vec)
		{
			return glm::vec2(vec.x, vec.y);
		}

		glm::vec3 Vector3From2(const glm::vec2& vec)
		{
			return glm::vec3(vec.x, vec.y, 0.0f);
		}

		float ToRadians(float degrees)
		{
			return degrees * PI / 180.0f;
		}

		float ToDegrees(float radians)
		{
			return radians * 180.0f / PI;
		}

		void Rotate(glm::vec2& vec, float angleDeg, const glm::vec2& origin)
		{
			float x = vec.x - origin.x;
			float y = vec.y - origin.y;

			float xPrime = origin.x + ((x * (float)cos(ToRadians(angleDeg))) - (y * (float)sin(ToRadians(angleDeg))));
			float yPrime = origin.y + ((x * (float)sin(ToRadians(angleDeg))) + (y * (float)cos(ToRadians(angleDeg))));

			vec.x = xPrime;
			vec.y = yPrime;
		}

		void Rotate(glm::vec3& vec, float angleDeg, const glm::vec3& origin)
		{
			// This function ignores Z values
			float x = vec.x - origin.x;
			float y = vec.y - origin.y;

			float xPrime = origin.x + ((x * (float)cos(ToRadians(angleDeg))) - (y * (float)sin(ToRadians(angleDeg))));
			float yPrime = origin.y + ((x * (float)sin(ToRadians(angleDeg))) + (y * (float)cos(ToRadians(angleDeg))));

			vec.x = xPrime;
			vec.y = yPrime;
		}

		float MapRange(float val, float inMin, float inMax, float outMin, float outMax)
		{
			return (val - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
		}

		int Max(int a, int b)
		{
			return a > b ? a : b;
		}

		int Min(int a, int b)
		{
			return a < b ? a : b;
		}

		float Saturate(float val)
		{
			return val < 0 ? 0 :
				val > 1 ? 1 :
				val;
		}

		uint32 HashString(const char* str)
		{
			uint32 hash = 2166136261u;
			int length = strlen(str);

			for (int i = 0; i < length; i++)
			{
				hash ^= str[i];
				hash *= 16777619;
			}

			return hash;
		}
	}
}