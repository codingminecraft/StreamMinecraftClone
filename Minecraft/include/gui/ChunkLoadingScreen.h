#ifndef MINECRAFT_CHUNK_LOADING_SCREEN_H
#define MINECRAFT_CHUNK_LOADING_SCREEN_H

namespace Minecraft
{
	namespace ChunkLoadingScreen
	{
		void init();

		void update(float dt, float percentLoaded);

		void free();
	}
}

#endif 