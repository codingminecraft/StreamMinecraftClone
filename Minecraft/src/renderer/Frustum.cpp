#include "renderer/Frustum.h"

namespace Minecraft
{
	// I yoinked this from here https://gist.github.com/podgorskiy/e698d18879588ada9014768e3e82a644
	// Until I have time to properly learn frustum culling this is how it shall stay
	Frustum::Frustum() {}

	// m = ProjectionMatrix * ViewMatrix 
	Frustum::Frustum(const glm::mat4& m)
	{
		update(m);
	}

	void Frustum::update(const glm::mat4& m)
	{
		glm::mat4 transposedM = glm::transpose(m);
		planes[Left] = transposedM[3] + transposedM[0];
		planes[Right] = transposedM[3] - transposedM[0];
		planes[Bottom] = transposedM[3] + transposedM[1];
		planes[Top] = transposedM[3] - transposedM[1];
		planes[Near] = transposedM[3] + transposedM[2];
		planes[Far] = transposedM[3] - transposedM[2];

		glm::vec3 crosses[Combinations] = {
			glm::cross(glm::vec3(planes[Left]),   glm::vec3(planes[Right])),
			glm::cross(glm::vec3(planes[Left]),   glm::vec3(planes[Bottom])),
			glm::cross(glm::vec3(planes[Left]),   glm::vec3(planes[Top])),
			glm::cross(glm::vec3(planes[Left]),   glm::vec3(planes[Near])),
			glm::cross(glm::vec3(planes[Left]),   glm::vec3(planes[Far])),
			glm::cross(glm::vec3(planes[Right]),  glm::vec3(planes[Bottom])),
			glm::cross(glm::vec3(planes[Right]),  glm::vec3(planes[Top])),
			glm::cross(glm::vec3(planes[Right]),  glm::vec3(planes[Near])),
			glm::cross(glm::vec3(planes[Right]),  glm::vec3(planes[Far])),
			glm::cross(glm::vec3(planes[Bottom]), glm::vec3(planes[Top])),
			glm::cross(glm::vec3(planes[Bottom]), glm::vec3(planes[Near])),
			glm::cross(glm::vec3(planes[Bottom]), glm::vec3(planes[Far])),
			glm::cross(glm::vec3(planes[Top]),    glm::vec3(planes[Near])),
			glm::cross(glm::vec3(planes[Top]),    glm::vec3(planes[Far])),
			glm::cross(glm::vec3(planes[Near]),   glm::vec3(planes[Far]))
		};

		points[0] = intersection<Left, Bottom, Near>(crosses);
		points[1] = intersection<Left, Top, Near>(crosses);
		points[2] = intersection<Right, Bottom, Near>(crosses);
		points[3] = intersection<Right, Top, Near>(crosses);
		points[4] = intersection<Left, Bottom, Far>(crosses);
		points[5] = intersection<Left, Top, Far>(crosses);
		points[6] = intersection<Right, Bottom, Far>(crosses);
		points[7] = intersection<Right, Top, Far>(crosses);
	}

	// http://iquilezles.org/www/articles/frustumcorrect/frustumcorrect.htm
	bool Frustum::isBoxVisible(const glm::vec3& minp, const glm::vec3& maxp) const
	{
		// check box outside/inside of frustum
		for (int i = 0; i < Count; i++)
		{
			if ((glm::dot(planes[i], glm::vec4(minp.x, minp.y, minp.z, 1.0f)) < 0.0) &&
				(glm::dot(planes[i], glm::vec4(maxp.x, minp.y, minp.z, 1.0f)) < 0.0) &&
				(glm::dot(planes[i], glm::vec4(minp.x, maxp.y, minp.z, 1.0f)) < 0.0) &&
				(glm::dot(planes[i], glm::vec4(maxp.x, maxp.y, minp.z, 1.0f)) < 0.0) &&
				(glm::dot(planes[i], glm::vec4(minp.x, minp.y, maxp.z, 1.0f)) < 0.0) &&
				(glm::dot(planes[i], glm::vec4(maxp.x, minp.y, maxp.z, 1.0f)) < 0.0) &&
				(glm::dot(planes[i], glm::vec4(minp.x, maxp.y, maxp.z, 1.0f)) < 0.0) &&
				(glm::dot(planes[i], glm::vec4(maxp.x, maxp.y, maxp.z, 1.0f)) < 0.0))
			{
				return false;
			}
		}

		// check frustum outside/inside box
		int out;
		out = 0; for (int i = 0; i < 8; i++) out += ((points[i].x > maxp.x) ? 1 : 0); if (out == 8) return false;
		out = 0; for (int i = 0; i < 8; i++) out += ((points[i].x < minp.x) ? 1 : 0); if (out == 8) return false;
		out = 0; for (int i = 0; i < 8; i++) out += ((points[i].y > maxp.y) ? 1 : 0); if (out == 8) return false;
		out = 0; for (int i = 0; i < 8; i++) out += ((points[i].y < minp.y) ? 1 : 0); if (out == 8) return false;
		out = 0; for (int i = 0; i < 8; i++) out += ((points[i].z > maxp.z) ? 1 : 0); if (out == 8) return false;
		out = 0; for (int i = 0; i < 8; i++) out += ((points[i].z < minp.z) ? 1 : 0); if (out == 8) return false;

		return true;
	}
}