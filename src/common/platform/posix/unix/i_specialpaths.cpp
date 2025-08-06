/*
** i_specialpaths.cpp
** Gets special system folders where data should be stored. (Unix version)
**
**---------------------------------------------------------------------------
** Copyright 2013-2016 Randy Heit
** Copyright 2016 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include <sys/stat.h>
#include <sys/types.h>
#include "i_system.h"
#include "cmdlib.h"
#include "printf.h"
#include "engineerrors.h"

#include "version.h"	// for GAMENAME

extern bool netgame;


FString GetUserFile (const char *file)
{
	FString path;
	struct stat info;

	path = NicePath("$HOME/" GAME_DIR "/");

	if (stat (path.GetChars(), &info) == -1)
	{
		struct stat extrainfo;

		// Sanity check for $HOME/.config
		FString configPath = NicePath("$HOME/.config/");
		if (stat (configPath.GetChars(), &extrainfo) == -1)
		{
			if (mkdir (configPath.GetChars(), S_IRUSR | S_IWUSR | S_IXUSR) == -1)
			{
				I_FatalError ("Failed to create $HOME/.config directory:\n%s", strerror(errno));
			}
		}
		else if (!S_ISDIR(extrainfo.st_mode))
		{
			I_FatalError ("$HOME/.config must be a directory");
		}

		// This can be removed after a release or two
		// Transfer the old zdoom directory to the new location
		bool moved = false;
		FString oldpath = NicePath("$HOME/." GAMENAMELOWERCASE "/");
		if (stat (oldpath.GetChars(), &extrainfo) != -1)
		{
			if (rename(oldpath.GetChars(), path.GetChars()) == -1)
			{
				I_Error ("Failed to move old " GAMENAMELOWERCASE " directory (%s) to new location (%s).",
					oldpath.GetChars(), path.GetChars());
			}
			else
				moved = true;
		}

		if (!moved && mkdir (path.GetChars(), S_IRUSR | S_IWUSR | S_IXUSR) == -1)
		{
			I_FatalError ("Failed to create %s directory:\n%s",
				path.GetChars(), strerror (errno));
		}
	}
	else
	{
		if (!S_ISDIR(info.st_mode))
		{
			I_FatalError ("%s must be a directory", path.GetChars());
		}
	}
	path += file;
	return path;
}

//===========================================================================
//
// M_GetAppDataPath														Unix
//
// Returns the path for the AppData folder.
//
//===========================================================================

FString M_GetAppDataPath(bool create)
{
	// Don't use GAME_DIR and such so that ZDoom and its child ports can
	// share the node cache.
	FString path = NicePath("$HOME/.config/" GAMENAMELOWERCASE);
	if (create)
	{
		CreatePath(path.GetChars());
	}
	return path;
}

//===========================================================================
//
// M_GetCachePath														Unix
//
// Returns the path for cache GL nodes.
//
//===========================================================================

FString M_GetCachePath(bool create)
{
	// Don't use GAME_DIR and such so that ZDoom and its child ports can
	// share the node cache.
	FString path = NicePath("$HOME/.config/zdoom/cache");
	if (create)
	{
		CreatePath(path.GetChars());
	}
	return path;
}

//===========================================================================
//
// M_GetAutoexecPath													Unix
//
// Returns the expected location of autoexec.cfg.
//
//===========================================================================

FString M_GetAutoexecPath()
{
	return GetUserFile("autoexec.cfg");
}

//===========================================================================
//
// M_GetConfigPath														Unix
//
// Returns the path to the config file. On Windows, this can vary for reading
// vs writing. i.e. If $PROGDIR/zdoom-<user>.ini does not exist, it will try
// to read from $PROGDIR/zdoom.ini, but it will never write to zdoom.ini.
//
//===========================================================================

FString M_GetConfigPath(bool for_reading)
{
	return GetUserFile(GAMENAMELOWERCASE ".ini");
}

//===========================================================================
//
// M_GetScreenshotsPath													Unix
//
// Returns the path to the default screenshots directory.
//
//===========================================================================

FString M_GetScreenshotsPath()
{
	return NicePath("$HOME/" GAME_DIR "/screenshots/");
}

//===========================================================================
//
// M_GetSavegamesPath													Unix
//
// Returns the path to the default save games directory.
//
//===========================================================================

FString M_GetSavegamesPath()
{
	FString pName = "$HOME/" GAME_DIR "/savegames/";
	if (netgame)
		pName << "netgame/";
	return NicePath(pName.GetChars());
}

//===========================================================================
//
// M_GetDocumentsPath												Unix
//
// Returns the path to the default documents directory.
//
//===========================================================================

FString M_GetDocumentsPath()
{
	return NicePath("$HOME/" GAME_DIR "/");
}

//===========================================================================
//
// M_GetDemoPath													Unix
//
// Returns the path to the default demo directory.
//
//===========================================================================

FString M_GetDemoPath()
{
	return M_GetDocumentsPath() + "demo/";
}

//===========================================================================
//
// M_NormalizedPath
//
// Normalizes the given path and returns the result.
//
//===========================================================================

FString M_GetNormalizedPath(const char* path)
{
	char *actualpath;
	actualpath = realpath(path, NULL);
	if (!actualpath) // error ?
		return nullptr;
	FString fullpath = actualpath;
	return fullpath;
}

//===========================================================================
//
// Crashcatcher is a C file. Thanks a lot for that! Really appreciate the
// quality work. Here's some more that fits so nicely into the codebase...
//
//===========================================================================

#include <libgen.h>
#include <unistd.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif
#ifdef EXTERN___PROGNAME
extern const char *__progname;
#endif

std::string exe_path()
{
	char exe_file[PATH_MAX];
#ifdef __APPLE__
	CFBundleRef mainBundle = CFBundleGetMainBundle();
	if (mainBundle) 
	{
		CFURLRef mainURL = CFBundleCopyBundleURL(mainBundle);
		
		if (mainURL) 
		{
			int ok = CFURLGetFileSystemRepresentation (
				mainURL, (Boolean) true, (UInt8*)exe_file, PATH_MAX 
			);
			
			if (ok)
			{
				return std::string(exe_file) + "/";
			}
		}
	}
	
	throw Exception("get_exe_path failed");

#else
#ifndef PROC_EXE_PATH
#define PROC_EXE_PATH "/proc/self/exe"
#endif
	int size;
	struct stat sb;
	if (lstat(PROC_EXE_PATH, &sb) < 0)
	{
#ifdef EXTERN___PROGNAME
		char *pathenv, *name, *end;
		char fname[PATH_MAX];
		char cwd[PATH_MAX];
		struct stat sba;

		exe_file[0] = '\0';
		if ((pathenv = getenv("PATH")) != nullptr)
		{
			for (name = pathenv; name; name = end)
			{
				if ((end = strchr(name, ':')))
					*end++ = '\0';
				snprintf(fname, sizeof(fname),
					"%s/%s", name, (char *)__progname);
				if (stat(fname, &sba) == 0) {
					snprintf(exe_file, sizeof(exe_file),
						"%s/", name);
					break;
				}
			}
		}
		// if getenv failed or path still not found
		// try current directory as last resort
		if (!exe_file[0])
		{
			if (getcwd(cwd, sizeof(cwd)) != nullptr)
			{
				snprintf(fname, sizeof(fname),
					"%s/%s", cwd, (char *)__progname);
				if (stat(fname, &sba) == 0)
					snprintf(exe_file, sizeof(exe_file),
						"%s/", cwd);
			}
		}
		if (!exe_file[0])
			return "";
		else
			return std::string(exe_file);
#else
		return "";
#endif
	}
	else
	{
		size = readlink(PROC_EXE_PATH, exe_file, PATH_MAX);
		if (size < 0)
		{
			return strerror(errno);
		}
		else
		{
			exe_file[size] = '\0';
			return std::string(dirname(exe_file)) + "/";
		}
	}
#endif
	
}

extern "C"
{
	const char *M_GetGameExe()
	{
		static FString s = (exe_path() + GAMENAMELOWERCASE).c_str();
		return s.GetChars();
	}
}
