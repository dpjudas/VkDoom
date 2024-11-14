/*
**  Windows Crash Reporter
**  Copyright (c) 2024 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>
#include <mutex>
#include <thread>
#include "i_mainwindow.h"

#ifdef _MSC_VER
#include <dbghelp.h>
#include <signal.h>
#include <Dbgeng.h>

#pragma comment(lib, "dbgeng.lib")

class CrashReporter
{
public:
	CrashReporter(const std::wstring& reports_directory, const std::wstring& uploader_executable = std::wstring());
	~CrashReporter();

	static void hook_thread();
	static void invoke();
	static void generate_report();

private:
	struct DumpParams
	{
		HANDLE hprocess;
		HANDLE hthread;
		int thread_id;
		PEXCEPTION_POINTERS exception_pointers;
		unsigned int exception_code;
	};

	void load_dbg_help();
	static void create_dump(DumpParams* params, bool launch_uploader);
	static std::wstring reports_directory;
	static std::wstring uploader_exe;

	static std::recursive_mutex mutex;

	static DWORD WINAPI create_dump_main(LPVOID thread_parameter);

	static int generate_report_filter(unsigned int code, struct _EXCEPTION_POINTERS* ep);
	static void on_terminate();
	static void on_sigabort(int);
	static void on_se_unhandled_exception(unsigned int exception_code, PEXCEPTION_POINTERS exception_pointers);
	static LONG WINAPI on_win32_unhandled_exception(PEXCEPTION_POINTERS exception_pointers);

	typedef BOOL(WINAPI* MiniDumpWriteDumpPointer)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, CONST PMINIDUMP_EXCEPTION_INFORMATION, CONST PMINIDUMP_USER_STREAM_INFORMATION, CONST PMINIDUMP_CALLBACK_INFORMATION);
	HMODULE module_dbghlp;
	static MiniDumpWriteDumpPointer func_MiniDumpWriteDump;

	bool enforce_filter(bool enforce);
	bool write_memory(BYTE* target, const BYTE* source, DWORD size);
#if defined(_M_X64)
	static const BYTE patch_bytes[6];
	BYTE original_bytes[6];
#elif defined(_M_IX86)
	static const BYTE patch_bytes[5];
	BYTE original_bytes[5];
#endif
};

#else

class CrashReporter
{
public:
	CrashReporter(const std::wstring& reports_directory, const std::wstring& uploader_executable = std::wstring()) { }

	static void hook_thread() {}
	static void invoke() { }
	static void generate_report() { }
};

#endif

void InitCrashReporter(const std::wstring& reports_directory, const std::wstring& uploader_executable)
{
	static CrashReporter reporter(reports_directory, uploader_executable);
}

void CrashReporterHookThread()
{
	CrashReporter::hook_thread();
}

void InvokeCrashReporter()
{
	CrashReporter::invoke();
}

#ifdef _MSC_VER

#pragma warning(disable: 4535) // warning C4535: calling _set_se_translator() requires /EHa

std::wstring CrashReporter::reports_directory;
std::wstring CrashReporter::uploader_exe;
CrashReporter::MiniDumpWriteDumpPointer CrashReporter::func_MiniDumpWriteDump = 0;
std::recursive_mutex CrashReporter::mutex;

CrashReporter::CrashReporter(const std::wstring& new_reports_directory, const std::wstring& new_uploader_executable)
{
	reports_directory = new_reports_directory;
	if (!reports_directory.empty() && (reports_directory.back() != L'/' && reports_directory.back() != L'\\'))
		reports_directory.push_back(L'\\');
	uploader_exe = new_uploader_executable;
	load_dbg_help();
	SetUnhandledExceptionFilter(&CrashReporter::on_win32_unhandled_exception);
	enforce_filter(true); // Prevent anyone else from changing the unhandled exception filter
	hook_thread();
}

CrashReporter::~CrashReporter()
{
}

void CrashReporter::invoke()
{
#ifdef _DEBUG
	DebugBreak(); // Bring up the debugger if it is running
#endif
	Sleep(1000); // Give any possibly logging functionality a little time before we die
	RaiseException(EXCEPTION_NONCONTINUABLE_EXCEPTION, EXCEPTION_NONCONTINUABLE_EXCEPTION, 0, 0);
}

void CrashReporter::generate_report()
{
	__try
	{
		RaiseException(EXCEPTION_BREAKPOINT, EXCEPTION_BREAKPOINT, 0, 0);
	}
	__except (generate_report_filter(GetExceptionCode(), GetExceptionInformation()))
	{
	}
}

void CrashReporter::create_dump(DumpParams* dump_params, bool launch_uploader)
{
	SYSTEMTIME systime;
	GetLocalTime(&systime);

	HANDLE file = INVALID_HANDLE_VALUE;
	WCHAR minidump_filename[1024];
	int counter;
	for (counter = 1; counter < 1000; counter++)
	{
		swprintf_s(minidump_filename, L"%s%04d-%02d-%02d %02d.%02d.%02d (%d).dmp", reports_directory.c_str(), systime.wYear, systime.wMonth, systime.wDay, systime.wHour, systime.wMinute, systime.wSecond, counter);
		file = CreateFile(minidump_filename, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
		if (file != INVALID_HANDLE_VALUE)
			break;
	}
	if (file == INVALID_HANDLE_VALUE)
		return;

	MINIDUMP_EXCEPTION_INFORMATION info;
	info.ThreadId = dump_params->thread_id;
	info.ExceptionPointers = dump_params->exception_pointers;
	info.ClientPointers = 0;
	MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
		MiniDumpWithHandleData |
		MiniDumpWithProcessThreadData |
		MiniDumpWithFullMemoryInfo |
		MiniDumpWithThreadInfo);
	func_MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &info, 0, 0);
	CloseHandle(file);

	WCHAR log_filename[1024];
	swprintf_s(log_filename, L"%s%04d-%02d-%02d %02d.%02d.%02d (%d).log", reports_directory.c_str(), systime.wYear, systime.wMonth, systime.wDay, systime.wHour, systime.wMinute, systime.wSecond, counter);

	file = CreateFile(log_filename, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (file == INVALID_HANDLE_VALUE)
		return;
	auto writeFile = [&](const void* data, uint32_t size, uint32_t& written) -> bool
		{
			DWORD tmp = 0;
			BOOL result = WriteFile(file, data, size, &tmp, nullptr);
			written = tmp;
			return result == TRUE;
		};
	mainwindow.GetLog(writeFile);
	CloseHandle(file);

	if (launch_uploader && !uploader_exe.empty())
	{
		WCHAR commandline[3 * 1024];
		swprintf_s(commandline, L"modulename -showcrashreport \"%s\" \"%s\"", minidump_filename, log_filename);

		STARTUPINFO startup_info = {};
		PROCESS_INFORMATION process_info = {};
		startup_info.cb = sizeof(STARTUPINFO);
		if (CreateProcess(uploader_exe.c_str(), commandline, 0, 0, FALSE, CREATE_SUSPENDED, 0, 0, &startup_info, &process_info))
		{
			// We need to allow the process to become the foreground window. Otherwise the error dialog while show up behind other apps
			AllowSetForegroundWindow(process_info.dwProcessId);
			ResumeThread(process_info.hThread);

			CloseHandle(process_info.hThread);
			CloseHandle(process_info.hProcess);

			Sleep(1000); // Give process one second to show the foreground window before we exit
		}
	}
}

int CrashReporter::generate_report_filter(unsigned int code, struct _EXCEPTION_POINTERS* ep)
{
	DumpParams dump_params;
	dump_params.hprocess = GetCurrentProcess();
	dump_params.hthread = GetCurrentThread();
	dump_params.thread_id = GetCurrentThreadId();
	dump_params.exception_pointers = ep;
	dump_params.exception_code = code;
	create_dump(&dump_params, false);
	return EXCEPTION_EXECUTE_HANDLER;
}

void CrashReporter::load_dbg_help()
{
	module_dbghlp = LoadLibrary(L"dbghelp.dll");
	if (module_dbghlp == 0)
		return;
	func_MiniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpPointer>(GetProcAddress(module_dbghlp, "MiniDumpWriteDump"));
}

void CrashReporter::hook_thread()
{
	set_terminate(&CrashReporter::on_terminate);
	_set_abort_behavior(0, _CALL_REPORTFAULT | _WRITE_ABORT_MSG);
	signal(SIGABRT, &CrashReporter::on_sigabort);
	_set_se_translator(&CrashReporter::on_se_unhandled_exception);
}

void CrashReporter::on_terminate()
{
	invoke();
}

void CrashReporter::on_sigabort(int)
{
	invoke();
}

void CrashReporter::on_se_unhandled_exception(unsigned int exception_code, PEXCEPTION_POINTERS exception_pointers)
{
	// Ignore those bloody breakpoints!
	if (exception_code == EXCEPTION_BREAKPOINT) return;

	DumpParams dump_params;
	dump_params.hprocess = GetCurrentProcess();
	dump_params.hthread = GetCurrentThread();
	dump_params.thread_id = GetCurrentThreadId();
	dump_params.exception_pointers = exception_pointers;
	dump_params.exception_code = exception_code;

	// Ensure we only get a dump of the first thread crashing - let other threads block here.
	std::unique_lock<std::recursive_mutex> mutex_lock(mutex);

	// Create dump in separate thread:
	DWORD threadId;
	HANDLE hThread = CreateThread(0, 0, &CrashReporter::create_dump_main, &dump_params, 0, &threadId);
	if (hThread != 0)
		WaitForSingleObject(hThread, INFINITE);
	TerminateProcess(GetCurrentProcess(), 255);
}

LONG CrashReporter::on_win32_unhandled_exception(PEXCEPTION_POINTERS exception_pointers)
{
	DumpParams dump_params;
	dump_params.hprocess = GetCurrentProcess();
	dump_params.hthread = GetCurrentThread();
	dump_params.thread_id = GetCurrentThreadId();
	dump_params.exception_pointers = exception_pointers;
	dump_params.exception_code = 0;

	// Ensure we only get a dump of the first thread crashing - let other threads block here.
	std::unique_lock<std::recursive_mutex> mutex_lock(mutex);

	// Create minidump in seperate thread:
	DWORD threadId;
	HANDLE hThread = CreateThread(0, 0, &CrashReporter::create_dump_main, &dump_params, 0, &threadId);
	if (hThread != 0)
		WaitForSingleObject(hThread, INFINITE);
	TerminateProcess(GetCurrentProcess(), 255);
	return EXCEPTION_EXECUTE_HANDLER;
}

DWORD WINAPI CrashReporter::create_dump_main(LPVOID thread_parameter)
{
	create_dump(reinterpret_cast<DumpParams*>(thread_parameter), true);
	TerminateProcess(GetCurrentProcess(), 255);
	return 0;
}

bool CrashReporter::enforce_filter(bool bEnforce)
{
#if defined(_M_X64) || defined(_M_IX86)
	DWORD ErrCode = 0;

	HMODULE hLib = GetModuleHandle(L"kernel32.dll");
	if (hLib == 0)
	{
		ErrCode = GetLastError();
		return false;
	}

	BYTE* pTarget = (BYTE*)GetProcAddress(hLib, "SetUnhandledExceptionFilter");
	if (pTarget == 0)
	{
		ErrCode = GetLastError();
		return false;
	}

	if (IsBadReadPtr(pTarget, sizeof(original_bytes)))
	{
		return false;
	}

	if (bEnforce)
	{
		// Save the original contents of SetUnhandledExceptionFilter 
		memcpy(original_bytes, pTarget, sizeof(original_bytes));

		// Patch SetUnhandledExceptionFilter 
		if (!write_memory(pTarget, patch_bytes, sizeof(patch_bytes)))
			return false;
	}
	else
	{
		// Restore the original behavior of SetUnhandledExceptionFilter 
		if (!write_memory(pTarget, original_bytes, sizeof(original_bytes)))
			return false;
	}
#endif

	return true;
}

bool CrashReporter::write_memory(BYTE* pTarget, const BYTE* pSource, DWORD Size)
{
	DWORD ErrCode = 0;

	if (pTarget == 0)
		return false;
	if (pSource == 0)
		return false;
	if (Size == 0)
		return false;
	if (IsBadReadPtr(pSource, Size))
		return false;

	// Modify protection attributes of the target memory page 

	DWORD OldProtect = 0;

	if (!VirtualProtect(pTarget, Size, PAGE_EXECUTE_READWRITE, &OldProtect))
	{
		ErrCode = GetLastError();
		return false;
	}

	// Write memory 

	memcpy(pTarget, pSource, Size);

	// Restore memory protection attributes of the target memory page 

	DWORD Temp = 0;

	if (!VirtualProtect(pTarget, Size, OldProtect, &Temp))
	{
		ErrCode = GetLastError();
		return false;
	}

	// Success 
	return true;
}

#if defined(_M_X64)
const BYTE CrashReporter::patch_bytes[6] = { 0x48, 0x31, 0xC0, 0xC2, 0x00, 0x00 };
#elif defined(_M_IX86)
const BYTE CrashReporter::patch_bytes[5] = { 0x33, 0xC0, 0xC2, 0x04, 0x00 };
#endif

#endif

template<typename T>
class DbgPtr
{
public:
	DbgPtr() { Ptr = nullptr; }
	DbgPtr(const DbgPtr& other) { Ptr = other.Ptr; if (Ptr) Ptr->AddRef(); }
	DbgPtr(DbgPtr&& move) { Ptr = move.Ptr; move.Ptr = nullptr; }
	~DbgPtr() { reset(); }

	void reset() { if (Ptr) Ptr->Release(); Ptr = nullptr; }
	T* get() { return Ptr; }

	static IID GetIID() { return __uuidof(T); }

	void** InitPtr() { return (void**)TypedInitPtr(); }
	T** TypedInitPtr() { reset(); return &Ptr; }

	DbgPtr& operator=(const DbgPtr& other)
	{
		if (this != &other)
		{
			if (Ptr)
				Ptr->Release();
			Ptr = other.Ptr;
			if (Ptr)
				Ptr->AddRef();
		}
		return *this;
	}

	operator T* () const { return Ptr; }
	T* operator ->() const { return Ptr; }

	T* Ptr;
};

void I_AddMinidumpCallstack(const FString& minidumpFilename, FString& text, FString& logText)
{
	DbgPtr<IDebugClient5> client;
	HRESULT result = DebugCreate(client.GetIID(), client.InitPtr());
	if (FAILED(result))
		return;

	DbgPtr<IDebugControl> control;
	result = client->QueryInterface(control.GetIID(), control.InitPtr());
	if (FAILED(result))
		return;

	DbgPtr<IDebugSymbols3> symbols;
	result = client->QueryInterface(symbols.GetIID(), symbols.InitPtr());
	if (FAILED(result))
		return;

	result = client->OpenDumpFileWide(minidumpFilename.WideString().c_str(), 0);
	if (FAILED(result))
		return;

	result = control->WaitForEvent(0, INFINITE);
	if (FAILED(result))
		return;

	ULONG eventType, processId = 0, threadId = 0, descriptionSize = 0;
	char descriptionText[1024] = {};
	result = control->GetLastEventInformation(&eventType, &processId, &threadId, nullptr, 0, nullptr, descriptionText, 1024, &descriptionSize);
	if (SUCCEEDED(result) && descriptionSize > 0)
	{
		std::string description(descriptionText, descriptionSize - 1);
		size_t uselessInfoPos = description.find(" (first/second chance not available)");
		if (uselessInfoPos != std::string::npos)
			description.resize(uselessInfoPos);
		text = description;
		logText.AppendFormat("\n%s\n", description.c_str());
	}

	DEBUG_STACK_FRAME frames[20] = {};
	ULONG filled = 0;
	result = control->GetStackTrace(0, 0, 0, frames, 20, &filled);
	if (FAILED(result))
		return;

	for (ULONG i = 0; i < filled; i++)
	{
		char nameBuffer[1024] = {};
		ULONG nameSize = 0;
		ULONG64 displacement = 0;
		result = symbols->GetNameByOffset(frames[i].InstructionOffset, nameBuffer, 1024, &nameSize, &displacement);
		if (SUCCEEDED(result) && nameSize > 0)
		{
			std::string name(nameBuffer, nameSize - 1);
			std::string module;
			size_t moduleend = name.find('!');
			if (moduleend != std::string::npos)
			{
				module = name.substr(0, moduleend);
				name = name.substr(moduleend + 1);
			}

			ULONG line = 0;
			char fileBuffer[1024] = {};
			ULONG fileSize = 0;
			ULONG64 displacement = 0;
			result = symbols->GetLineByOffset(frames[i].InstructionOffset, &line, fileBuffer, 1024, &fileSize, &displacement);
			if (SUCCEEDED(result) && fileSize > 0)
			{
				std::string file(fileBuffer, fileSize - 1);
				size_t lastpart = file.find_last_of("/\\");
				if (lastpart != std::string::npos)
					file = file.substr(lastpart + 1);

				logText.AppendFormat("Called from %s at %s, line %d\n", name.c_str(), file.c_str(), line);
			}
			else
			{
				// logText.AppendFormat("Called from %s in module %s\n", name.c_str(), module.c_str());
			}
		}
		else
		{
			// logText.AppendFormat("Called from %d\n", frames[i].InstructionOffset);
		}
	}
}
