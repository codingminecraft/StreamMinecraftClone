#ifndef MINECRAFT_FILE_H
#define MINECRAFT_FILE_H

namespace Minecraft
{
	namespace File
	{
		bool removeDir(const char* directoryName);
		bool isDir(const char* directoryName);
		bool isFile(const char* directoryName);
		bool moveFile(const char* from, const char* to);
		bool createDirIfNotExists(const char* directoryName);
	}
}

#endif 