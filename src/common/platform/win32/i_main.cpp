/*
** i_main.cpp
** System-specific startup code. Eventually calls D_DoomMain.
**
**---------------------------------------------------------------------------
** Copyright 1998-2009 Randy Heit
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>
#include <commctrl.h>
#include <string>
#include <ShlObj.h>

#include <processenv.h>
#include <shellapi.h>
#include <VersionHelpers.h>

#ifdef _MSC_VER
#pragma warning(disable:4244)
#endif

#ifdef _MSC_VER
#include <eh.h>
#include <new.h>
#include <crtdbg.h>
#endif
#include "resource.h"

#include "engineerrors.h"
#include "hardware.h"

#include "m_argv.h"
#include "i_module.h"
#include "c_console.h"
#include "version.h"
#include "i_input.h"
#include "filesystem.h"
#include "cmdlib.h"
#include "s_soundinternal.h"
#include "vm.h"
#include "i_system.h"
#include "gstrings.h"
#include "s_music.h"

#include "stats.h"
#include "st_start.h"
#include "i_interface.h"
#include "startupinfo.h"
#include "printf.h"

#include "i_mainwindow.h"

// The main window's title.
#ifdef _M_X64
#define X64 " 64-bit"
#elif _M_ARM64
#define X64 " ARM-64"
#else
#define X64 ""
#endif

void InitCrashReporter(const std::wstring& reports_directory, const std::wstring& uploader_executable);
FString GetKnownFolder(int shell_folder, REFKNOWNFOLDERID known_folder, bool create);

void DestroyCustomCursor();
int GameMain();

extern UINT TimerPeriod;

// The command line arguments.
FArgs *Args;

HINSTANCE		g_hInst;
HANDLE			StdOut;
bool			FancyStdOut, AttachedStdOut;

void I_SetIWADInfo()
{
}

//==========================================================================

int DoMain (HINSTANCE hInstance)
{
	LONG WinWidth, WinHeight;
	int height, width, x, y;
	RECT cRect;
	TIMECAPS tc;
	DEVMODE displaysettings;

	// Do not use the multibyte __argv here because we want UTF-8 arguments
	// and those can only be done by converting the Unicode variants.
	Args = new FArgs();
	auto argc = __argc;
	auto wargv = __wargv;
	for (int i = 0; i < argc; i++)
	{
		Args->AppendArg(FString(wargv[i]));
	}

	if (Args->CheckParm("-stdout") || Args->CheckParm("-norun"))
	{
		// As a GUI application, we don't normally get a console when we start.
		// If we were run from the shell and are on XP+, we can attach to its
		// console. Otherwise, we can create a new one. If we already have a
		// stdout handle, then we have been redirected and should just use that
		// handle instead of creating a console window.

		StdOut = GetStdHandle(STD_OUTPUT_HANDLE);
		if (StdOut != nullptr)
		{
			// It seems that running from a shell always creates a std output
			// for us, even if it doesn't go anywhere. (Running from Explorer
			// does not.) If we can get file information for this handle, it's
			// a file or pipe, so use it. Otherwise, pretend it wasn't there
			// and find a console to use instead.
			BY_HANDLE_FILE_INFORMATION info;
			if (!GetFileInformationByHandle(StdOut, &info))
			{
				StdOut = nullptr;
			}
		}
		if (StdOut == nullptr)
		{
			if (AttachConsole(ATTACH_PARENT_PROCESS))
			{
				StdOut = GetStdHandle(STD_OUTPUT_HANDLE);
				DWORD foo; WriteFile(StdOut, "\n", 1, &foo, nullptr);
				AttachedStdOut = true;
			}
			if (StdOut == nullptr && AllocConsole())
			{
				StdOut = GetStdHandle(STD_OUTPUT_HANDLE);
			}
			if (StdOut != nullptr)
			{
				SetConsoleCP(CP_UTF8);
				SetConsoleOutputCP(CP_UTF8);
				DWORD mode;
				if (GetConsoleMode(StdOut, &mode))
				{
					if (SetConsoleMode(StdOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
						FancyStdOut = IsWindows10OrGreater(); // Windows 8.1 and lower do not understand ANSI formatting.
				}
			}
		}
	}

	// Set the timer to be as accurate as possible
	if (timeGetDevCaps (&tc, sizeof(tc)) != TIMERR_NOERROR)
		TimerPeriod = 1;	// Assume minimum resolution of 1 ms
	else
		TimerPeriod = tc.wPeriodMin;

	timeBeginPeriod (TimerPeriod);
	atexit([](){ timeEndPeriod(TimerPeriod); });

	// Figure out what directory the program resides in.
	WCHAR progbuff[1024];
	if (GetModuleFileNameW(nullptr, progbuff, 1024) == 0)
	{
		MessageBoxA(nullptr, "Fatal", "Could not determine program location.", MB_ICONEXCLAMATION|MB_OK);
		exit(-1);
	}

	progbuff[1023] = '\0';
	if (auto lastsep = wcsrchr(progbuff, '\\'))
	{
		lastsep[1] = '\0';
	}

	progdir = progbuff;
	FixPathSeperator(progdir);

	HDC screenDC = GetDC(0);
	int dpi = GetDeviceCaps(screenDC, LOGPIXELSX);
	ReleaseDC(0, screenDC);
	width = (512 * dpi + 96 / 2) / 96;
	height = (384 * dpi + 96 / 2) / 96;

	// Many Windows structures that specify their size do so with the first
	// element. DEVMODE is not one of those structures.
	memset (&displaysettings, 0, sizeof(displaysettings));
	displaysettings.dmSize = sizeof(displaysettings);
	EnumDisplaySettings (nullptr, ENUM_CURRENT_SETTINGS, &displaysettings);
	x = (displaysettings.dmPelsWidth - width) / 2;
	y = (displaysettings.dmPelsHeight - height) / 2;

	if (Args->CheckParm ("-0"))
	{
		x = y = 0;
	}

	/* create window */
	FStringf caption("" GAMENAME " %s " X64 " (%s)", GetVersionString(), GetGitTime());
	mainwindow.Create(caption, x, y, width, height);

	GetClientRect (mainwindow.GetHandle(), &cRect);

	WinWidth = cRect.right;
	WinHeight = cRect.bottom;

	int ret = GameMain ();

	if (mainwindow.CheckForRestart())
	{
		HMODULE hModule = GetModuleHandleW(nullptr);
		WCHAR path[MAX_PATH];
		GetModuleFileNameW(hModule, path, MAX_PATH);
		ShellExecuteW(nullptr, L"open", path, GetCommandLineW(), nullptr, SW_SHOWNORMAL);
	}

	DestroyCustomCursor();
	if (ret == 1337) // special exit code for 'norun'.
	{
		if (!batchrun)
		{
			if (FancyStdOut && !AttachedStdOut)
			{ // Outputting to a new console window: Wait for a keypress before quitting.
				DWORD bytes;
				HANDLE stdinput = GetStdHandle(STD_INPUT_HANDLE);

				ShowWindow(mainwindow.GetHandle(), SW_HIDE);
				if (StdOut != nullptr) WriteFile(StdOut, "Press any key to exit...", 24, &bytes, nullptr);
				FlushConsoleInputBuffer(stdinput);
				SetConsoleMode(stdinput, 0);
				ReadConsole(stdinput, &bytes, 1, &bytes, nullptr);
			}
			else if (StdOut == nullptr)
			{
				mainwindow.ShowErrorPane(nullptr);
			}
		}
	}
	return ret;
}

void I_ShowFatalError(const char *msg)
{
	I_ShutdownGraphics ();
	mainwindow.RestoreConView();
	S_StopMusic(true);

	if (CVMAbortException::stacktrace.IsNotEmpty())
	{
		Printf("%s", CVMAbortException::stacktrace.GetChars());
	}

	if (!batchrun)
	{
		mainwindow.ShowErrorPane(msg);
	}
	else
	{
		Printf("%s\n", msg);
	}
}

//==========================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE nothing, LPWSTR cmdline, int nCmdShow)
{
	g_hInst = hInstance;

	InitCommonControls();

	if (SUCCEEDED(CoInitialize(nullptr)))
		atexit([]() { CoUninitialize(); }); // beware of calling convention.

#if defined(_DEBUG) && defined(_MSC_VER)
	// Uncomment this line to make the Visual C++ CRT check the heap before
	// every allocation and deallocation. This will be slow, but it can be a
	// great help in finding problem areas.
	//_CrtSetDbgFlag (_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF);

	// Enable leak checking at exit.
	_CrtSetDbgFlag (_CrtSetDbgFlag(0) | _CRTDBG_LEAK_CHECK_DF);

	// Use this to break at a specific allocation number.
	//_crtBreakAlloc = 227524;
#endif

	// Setup crash reporting, unless it is the crash reporter launching us in response to a crash
	if (wcsstr(cmdline, L"-showcrashreport") == nullptr)
	{
		WCHAR exeFilename[1024] = {};
		if (GetModuleFileName(0, exeFilename, 1023) != 0)
		{
			FString reportsDirectory = GetKnownFolder(CSIDL_LOCAL_APPDATA, FOLDERID_LocalAppData, true);
			reportsDirectory += "/" GAMENAMELOWERCASE;
			reportsDirectory += "/crashreports";
			CreatePath(reportsDirectory.GetChars());

			InitCrashReporter(reportsDirectory.WideString(), exeFilename);
		}
	}

	return DoMain(hInstance);
}

void I_SetWindowTitle(const char* caption)
{
	mainwindow.SetWindowTitle(caption);
}
