#ifndef MINECRAFT_FILE_H
#define MINECRAFT_FILE_H

namespace Minecraft
{
	struct FileTime
	{
		uint64 creation;
		uint64 lastWrite;
		uint64 lastAccess;
	};

	namespace File
	{
		bool removeDir(const char* directoryName);
		bool isDir(const char* directoryName);
		bool isFile(const char* directoryName);
		bool moveFile(const char* from, const char* to);
		bool createDirIfNotExists(const char* directoryName);
		std::string getSpecialAppFolder();

		/// <summary>
		/// Gets the all pertinent time metrics of a file.
		/// </summary>
		/// <param name="fileOrDirName"></param>
		/// <returns>
		/// FileTime structure with times UINT64_MAX on failure, otherwise the time in 100 nanosecond intervals
		///	since 12:00 AM Jan. 1, 1601
		/// </returns>
		FileTime getFileTimes(const char* fileOrDirName);
	}
}

#endif 