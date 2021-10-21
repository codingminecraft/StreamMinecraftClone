#ifndef MINECRAFT_FRUSTUM_H
#define MINECRAFT_FRUSTUM_H
#include "core.h"

namespace Minecraft
{
	// I yoinked this from here https://gist.github.com/podgorskiy/e698d18879588ada9014768e3e82a644
	// Until I have time to properly learn frustum culling this is how it shall stay
	class Frustum
	{
	public:
		Frustum();
		Frustum(const glm::mat4& m);

		void update(const glm::mat4& m);
		bool isBoxVisible(const glm::vec3& minp, const glm::vec3& maxp) const;

	private:
		enum Planes
		{
			Left = 0,
			Right,
			Bottom,
			Top,
			Near,
			Far,
			Count,
			Combinations = Count * (Count - 1) / 2
		};

		template<Planes i, Planes j>
		struct ij2k
		{
			enum { k = i * (9 - i) / 2 + j - 1 };
		};

		template<Planes a, Planes b, Planes c>
		glm::vec3 intersection(const glm::vec3* crosses) const
		{
			float D = glm::dot(glm::vec3(planes[a]), crosses[ij2k<b, c>::k]);
			glm::vec3 res = glm::mat3(crosses[ij2k<b, c>::k], -crosses[ij2k<a, c>::k], crosses[ij2k<a, b>::k]) *
				glm::vec3(planes[a].w, planes[b].w, planes[c].w);
			return res * (-1.0f / D);
		}

		glm::vec4 planes[Count];
		glm::vec3 points[8];
	};
}

#endif