#ifndef FILE_H__
#define FILE_H__

bool DeleteDirectory(const std::string&, bool noRecycleBin = true);

#ifdef WINMOBILE
	// Tested before WIN32 because CeGCC predefines WIN32, and the branch below
	// does not survive the platform: shell32 does not exist on Windows CE, so
	// SHFileOperation/SHFILEOPSTRUCT are absent from the SDK and would not link
	// even if they were declared.  wce_deleteTree walks the tree with
	// FindFirstFileW/RemoveDirectoryW instead.
	//
	// There is no recycle bin on CE either, so noRecycleBin has nothing to
	// select: the delete is always permanent, which is what every caller in the
	// game asks for.
	//
	// <windows.h> is included even though nothing here needs it, to keep the
	// same transitive-include contract as the WIN32 branch below: callers such
	// as ExternalFileLevelStorageSource.cpp get their Win32 declarations through
	// this header and nowhere else.
	#include "wince_compat.h"
	#include <windows.h>
	#include <string>

bool DeleteDirectory(const std::string& dir, bool /*noRecycleBin*/)
{
	return wce_deleteTree(dir.c_str());
}
#elif defined(WIN32)
    #include <windows.h>
    #include <tchar.h>
    #include <shellapi.h>
    #include <string>

bool DeleteDirectory(const std::string& dir, bool noRecycleBin /*true*/)
{
    int len = strlen(dir.c_str());
    //TCHAR *pszFrom = new TCHAR[len+2];
	char* pszFrom = new char[len+2];
    strncpy(pszFrom, dir.c_str(), len);
    pszFrom[len] = 0;
    pszFrom[len+1] = 0;

    SHFILEOPSTRUCT fileop;
    fileop.hwnd   = NULL;    // no status display
    fileop.wFunc  = FO_DELETE;  // delete operation
    fileop.pFrom  = pszFrom;  // source file name as double null terminated string
    fileop.pTo    = NULL;    // no destination needed
    fileop.fFlags = FOF_NOCONFIRMATION|FOF_SILENT;  // do not prompt the user

    if(!noRecycleBin)
		fileop.fFlags |= FOF_ALLOWUNDO;

    fileop.fAnyOperationsAborted = FALSE;
    fileop.lpszProgressTitle     = NULL;
    fileop.hNameMappings         = NULL;

    int ret = SHFileOperation(&fileop);
    delete [] pszFrom;
    return (ret == 0);
}
#else
	#include <cstdio>
	#include <dirent.h>

bool DeleteDirectory(const std::string& d, bool noRecycleBin /*true*/)
{
	const char* folder = d.c_str();

	const size_t CMAX = 1024;
	char fullPath[CMAX];
	DIR* dir = opendir(folder);
	if (!dir)
		return false;

	struct dirent *entry;
	while ((entry = readdir(dir))) {
		if (strcmp(".", entry->d_name) && strcmp("..", entry->d_name)) {
			snprintf(fullPath, CMAX, "%s/%s", folder, entry->d_name);
			remove(fullPath);
		}
	}

	closedir(dir);

	return remove(folder) == 0;
}

#endif /*(ELSE) WIN32*/

#endif /*FILE_H__*/
