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
	}
}

#endif