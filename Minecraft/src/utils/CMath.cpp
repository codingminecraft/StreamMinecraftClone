#include "utils/CMath.h"

namespace Minecraft
{
	namespace CMath
	{
		static float PI = 3.1415926535897932384626433832795028841971693993751058209749445923078164062f;

		bool compare(float x, float y, float epsilon)
		{
			return abs(x - y) <= epsilon * std::max(1.0f, std::max(abs(x), abs(y)));
		}

		bool compare(const glm::vec2& vec1, const glm::vec2& vec2, float epsilon)
		{
			return compare(vec1.x, vec2.x, epsilon) && compare(vec1.y, vec2.y, epsilon);
		}

		bool compare(const glm::vec3& vec1, const glm::vec3& vec2, float epsilon)
		{
			return compare(vec1.x, vec2.x, epsilon) && compare(vec1.y, vec2.y, epsilon) && compare(vec1.z, vec2.z, epsilon);
		}

		bool compare(const glm::vec4& vec1, const glm::vec4& vec2, float epsilon)
		{
			return compare(vec1.x, vec2.x, epsilon) && compare(vec1.y, vec2.y, epsilon) && compare(vec1.z, vec2.z, epsilon) && compare(vec1.w, vec2.w, epsilon);
		}

		glm::vec2 vector2From3(const glm::vec3& vec)
		{
			return glm::vec2(vec.x, vec.y);
		}

		glm::vec3 vector3From2(const glm::vec2& vec)
		{
			return glm::vec3(vec.x, vec.y, 0.0f);
		}

		float toRadians(float degrees)
		{
			return degrees * PI / 180.0f;
		}

		float toDegrees(float radians)
		{
			return radians * 180.0f / PI;
		}

		void rotate(glm::vec2& vec, float angleDeg, const glm::vec2& origin)
		{
			float x = vec.x - origin.x;
			float y = vec.y - origin.y;

			float xPrime = origin.x + ((x * (float)cos(toRadians(angleDeg))) - (y * (float)sin(toRadians(angleDeg))));
			float yPrime = origin.y + ((x * (float)sin(toRadians(angleDeg))) + (y * (float)cos(toRadians(angleDeg))));

			vec.x = xPrime;
			vec.y = yPrime;
		}

		void rotate(glm::vec3& vec, float angleDeg, const glm::vec3& origin)
		{
			// This function ignores Z values
			float x = vec.x - origin.x;
			float y = vec.y - origin.y;

			float xPrime = origin.x + ((x * (float)cos(toRadians(angleDeg))) - (y * (float)sin(toRadians(angleDeg))));
			float yPrime = origin.y + ((x * (float)sin(toRadians(angleDeg))) + (y * (float)cos(toRadians(angleDeg))));

			vec.x = xPrime;
			vec.y = yPrime;
		}

		float mapRange(float val, float inMin, float inMax, float outMin, float outMax)
		{
			return (val - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
		}

		int max(int a, int b)
		{
			return a > b ? a : b;
		}

		int min(int a, int b)
		{
			return a < b ? a : b;
		}

		float saturate(float val)
		{
			return val < 0 ? 0 :
				val > 1 ? 1 :
				val;
		}

		uint32 hashString(const char* str)
		{
			uint32 hash = 2166136261u;
			int length = (int)strlen(str);

			for (int i = 0; i < length; i++)
			{
				hash ^= str[i];
				hash *= 16777619;
			}

			return hash;
		}

		std::string toString(const glm::vec4& vec4, int precision)
		{
			return std::string("(w: ")
				+ toString(vec4.w, precision) + ",x: "
				+ toString(vec4.x, precision) + ",y: "
				+ toString(vec4.y, precision) + ",z: "
				+ toString(vec4.z, precision) + ")";
		}

		std::string toString(const glm::vec3& vec3, int precision)
		{
			return std::string("(x: ")
				+ toString(vec3.x, precision) + ",y: "
				+ toString(vec3.y, precision) + ",z: "
				+ toString(vec3.z, precision) + ")";
		}

		std::string toString(const glm::vec2& vec2, int precision)
		{
			return std::string("(x: ")
				+ toString(vec2.x, precision) + ",y: "
				+ toString(vec2.y, precision) + ")";
		}

		std::string toString(const glm::ivec4& vec4)
		{
			return std::string("(w: ")
				+ std::to_string(vec4.w) + ",x: "
				+ std::to_string(vec4.x) + ",y: "
				+ std::to_string(vec4.y) + ",z: "
				+ std::to_string(vec4.z) + ")";
		}

		std::string toString(const glm::ivec3& vec3)
		{
			return std::string("(x: ")
				+ std::to_string(vec3.x) + ",y: "
				+ std::to_string(vec3.y) + ",z: "
				+ std::to_string(vec3.z) + ")";
		}

		std::string toString(const glm::ivec2& vec2)
		{
			return std::string("(x: ")
				+ std::to_string(vec2.x) + ",y: "
				+ std::to_string(vec2.y) + ")";
		}

		std::string toString(float value, int precision)
		{
			const std::string str = std::to_string(value);
			const int precisionIndex = (int)str.find(".") + precision + 1;
			return str.substr(0, precisionIndex);
		}

		int length2(const glm::ivec2& vec) 
		{
			return vec.x * vec.x + vec.y * vec.y;
		}

		int length2(const glm::ivec3& vec)
		{
			return vec.x * vec.x + vec.y * vec.y + vec.z * vec.z;
		}

		int length2(const glm::ivec4& vec)
		{
			return vec.x * vec.x + vec.y * vec.y + vec.z * vec.z + vec.w * vec.w;
		}

		int negativeMod(int value, int lowerBound, int upperBound)
		{
			int rangeSize = upperBound - lowerBound + 1;

			if (value < lowerBound)
			{
				value += rangeSize * ((lowerBound - value) / rangeSize + 1);
			}

			return lowerBound + (value - lowerBound) % rangeSize;
		}
	}
}