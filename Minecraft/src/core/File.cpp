#include "core.h"
#include "core/File.h"

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#include <shobjidl_core.h>
#include <shlobj.h>

namespace Minecraft
{
	namespace File
	{
		bool removeDir(const char* directoryName)
		{
			static char buffer[FILENAME_MAX];
			strcpy(buffer, directoryName);
			buffer[strlen(directoryName)] = '\0';

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

		bool isDir(const char* directoryName)
		{
			DWORD fileAttrib = GetFileAttributesA(directoryName);
			return (fileAttrib != INVALID_FILE_ATTRIBUTES && (fileAttrib & FILE_ATTRIBUTE_DIRECTORY));
		}

		bool isFile(const char* directoryName)
		{
			DWORD fileAttrib = GetFileAttributesA(directoryName);
			return (fileAttrib != INVALID_FILE_ATTRIBUTES && !(fileAttrib & FILE_ATTRIBUTE_DIRECTORY));
		}

		bool moveFile(const char* from, const char* to)
		{
			if (!MoveFileExA(from, to, MOVEFILE_WRITE_THROUGH))
			{
				g_logger_error("Move file failed with %d", GetLastError());
				return false;
			}

			return true;
		}

		bool createDirIfNotExists(const char* directoryName)
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

		std::string getSpecialAppFolder()
		{
			static std::string specialAppFolder = "";

			PWSTR pszPath;
			if (specialAppFolder == "" && SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &pszPath)))
			{
				// TODO: PWSTR uses wchar_t* not char_t* fix this
				// TODO: Query the registry for maximum path length
				static char tmp[MAX_PATH];
				wcstombs(tmp, pszPath, 256);
				specialAppFolder = tmp;
				CoTaskMemFree(pszPath);
			}

			return specialAppFolder;
		}

		FileTime getFileTimes(const char* fileOrDirName)
		{
			int dwFlagsAndAttributes = 0;
			if (isDir(fileOrDirName))
			{
				dwFlagsAndAttributes = FILE_FLAG_BACKUP_SEMANTICS;
			}
			HANDLE fileHandle = CreateFileA(fileOrDirName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, dwFlagsAndAttributes, NULL);

			if (fileHandle == INVALID_HANDLE_VALUE)
			{
				g_logger_error("Could not get file times for file '%s'. Failed to open file.", fileOrDirName);
				return { UINT64_MAX, UINT64_MAX, UINT64_MAX };
			}

			FILETIME create, lastAccess, lastWrite;
			if (!GetFileTime(fileHandle, &create, &lastAccess, &lastWrite))
			{
				g_logger_error("Could not get file times for file '%s'. Failed to get file time metrics.", fileOrDirName);
				return { UINT64_MAX, UINT64_MAX, UINT64_MAX };
			}

			FileTime res;
			res.creation = ULARGE_INTEGER{ create.dwLowDateTime, create.dwHighDateTime }.QuadPart;
			res.lastAccess = ULARGE_INTEGER{ lastAccess.dwLowDateTime, lastAccess.dwHighDateTime }.QuadPart;
			res.lastWrite = ULARGE_INTEGER{ lastWrite.dwLowDateTime, lastWrite.dwHighDateTime }.QuadPart;
			CloseHandle(fileHandle);
			return res;
		}
	}
}

// end _WIN_32
#elif defined(__linux__) 
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>


namespace Minecraft
{
	namespace File
	{
		bool removeDir(const char* directoryName)
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
								result2 = removeDir(buffer);
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

		bool isDir(const char* directoryName)
		{
			struct stat path_stat;
			stat(directoryName, &path_stat);
			return S_ISDIR(path_stat.st_mode);
		}

		bool isFile(const char* directoryName)
		{
			struct stat path_stat;
			stat(directoryName, &path_stat);
			return S_ISREG(path_stat.st_mode);
		}

		bool moveFile(const char* from, const char* to)
		{
			return rename(from, to) == 0;
		}

		bool createDirIfNotExists(const char* directoryName)
		{
			if (!isDir(directoryName)) 
			{
				if (!isFile(directoryName)) 
				{
					if (mkdir(directoryName,  0777) != 0)
					{
						const char* error = errno == EACCES ? "Search permission is denied."
							: errno == EEXIST ? "The named file exists." 
							: errno == ELOOP ? "A loop exists in the symbolic link."
							: errno == EMLINK ? "EMLINK"
							: errno == ENAMETOOLONG ? "ENAMETOOLONG"
							: errno == ENOENT ? "ENOENT"
							: errno == ENOSPC ? "ENOSPC"
							: errno == ENOTDIR ? "ENOTDIR"
							: errno == EROFS ? "EROFS"
							: "Unknown";
						g_logger_error("Make directory '%s' failed with error code: %s:%d", directoryName, error, errno);
						return false;
					}
					return true;
				}
				else 
				{
					g_logger_error("Cannot make '%s' a directory. A file with that name already exists.", directoryName);
					return false;
				}
			}
			else 
			{
				g_logger_info("Not creating '%s' because it is already a directory.", directoryName);
			}
			
			return true;
		}

		std::string getSpecialAppFolder()
		{
			const char* homedir;

			if ((homedir = getenv("HOME")) == NULL) 
			{
				homedir = getpwuid(getuid())->pw_dir;
			}

			return homedir;
		}

		FileTime getFileTimes(const char* fileOrDirName)
		{
			struct stat attr;
			if (stat(fileOrDirName, &attr) == 0) 
			{
				FileTime res;
				res.creation = (uint64)attr.st_ctime;
				res.lastAccess = (uint64)attr.st_atime;
				res.lastWrite = (uint64)attr.st_mtime;
				return res;
			}

			return { UINT64_MAX, UINT64_MAX, UINT64_MAX };
		}
	}
}

// end __linux__
#endif