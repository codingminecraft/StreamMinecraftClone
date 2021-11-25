#include "File.h"

#include <string>
#include <cppUtils/cppUtils.hpp>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>

bool manim_remove_dir(const char* directoryName)
{
	static char buffer[FILENAME_MAX];
	strcpy(buffer, directoryName);
	buffer[strlen(directoryName) + 1] = '\0';

	SHFILEOPSTRUCTA op = {
		NULL,
		FO_DELETE,
		buffer,
		NULL,
		FOF_SILENT | FOF_NOCONFIRMATION,
		NULL,
		NULL,
		NULL
	};
	if (SHFileOperationA(&op) != 0)
	{
		g_logger_error("Removing directory '%s' failed with '%d'", directoryName, GetLastError());
		return false;
	}

	return true;
}

bool manim_is_dir(const char* directoryName)
{
	DWORD fileAttrib = GetFileAttributesA(directoryName);
	return (fileAttrib != INVALID_FILE_ATTRIBUTES && (fileAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool manim_is_file(const char* directoryName)
{
	DWORD fileAttrib = GetFileAttributesA(directoryName);
	return (fileAttrib != INVALID_FILE_ATTRIBUTES && !(fileAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool manim_move_file(const char* from, const char* to)
{
	if (!MoveFileExA(from, to, MOVEFILE_WRITE_THROUGH))
	{
		g_logger_error("Move file failed with %d", GetLastError());
		return false;
	}

	return true;
}

bool manim_create_dir_if_not_exists(const char* directoryName)
{
	DWORD fileAttrib = GetFileAttributesA(directoryName);
	if (fileAttrib == INVALID_FILE_ATTRIBUTES)
	{
		if (!CreateDirectoryA(directoryName, NULL))
		{
			g_logger_error("Creating tmp directory failed with error code '%d'", GetLastError());
			return false;
		}
	}
	
	return true;
}

#elif defined(__linux__)
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

bool manim_remove_dir(const char* directoryName)
{
	DIR* directory = opendir(directoryName);
	size_t pathLength = strlen(directoryName);
	int result = -1;

	if (directory) 
	{
		struct dirent* parent;

		result = 0;
		while (!result && (parent=readdir(directory))) 
		{
			int result2 = -1;
			char* buffer;
			size_t length;

			/* Skip the names "." and ".." as we don't want to recurse on them. */
			if (!strcmp(parent->d_name, ".") || !strcmp(parent->d_name, ".."))
				continue;

			length = pathLength + strlen(parent->d_name) + 2; 
			buffer = (char*)malloc(length);

			if (buffer) 
			{
				struct stat statbuf;

				snprintf(buffer, length, "%s/%s", directoryName, parent->d_name);
				if (!stat(buffer, &statbuf)) 
				{
					if (S_ISDIR(statbuf.st_mode))
					{
						result2 = manim_remove_dir(buffer);
					}
					else
					{
						result2 = unlink(buffer);
					}
				}
				free(buffer);
			}
			result = result2;
		}
		closedir(directory);
	}

	if (result == 0)
	{
		result = rmdir(directoryName);
	}

	return result == 0;
}

bool manim_is_dir(const char* directoryName)
{
	struct stat path_stat;
    stat(directoryName, &path_stat);
    return S_ISDIR(path_stat.st_mode);
}

bool manim_is_file(const char* directoryName)
{
	struct stat path_stat;
    stat(directoryName, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

bool manim_move_file(const char* from, const char* to)
{
	return rename(from, to) == 0;
}

bool manim_create_dir_if_not_exists(const char* directoryName)
{
	if (!manim_is_dir(directoryName)) 
	{
		if (!manim_is_file(directoryName)) 
		{
			return mkdir(directoryName,  S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0;
		}
		else 
		{
			g_logger_error("Cannot make '%s' a directory. A file with that name already exists.", directoryName);
			return false;
		}
	}
	
	return true;
}

#endif