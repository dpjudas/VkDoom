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
#include "version.h"
#include "c_console.h"

#ifdef _MSC_VER
#include <dbghelp.h>
#include <signal.h>

#if defined(USE_DBGENG)
#include <Dbgeng.h>
#pragma comment(lib, "dbgeng.lib")
#else
#pragma comment(lib, "dbghelp.lib")
#endif

struct StackFrameList
{
	enum { max_frames = 16 };
	int frame_count;
	void* frames[max_frames];
};

enum { UserStackFrameListStream = LastReservedStream + 1 };

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
		StackFrameList stack_frames;
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

	static int CaptureStackTrace(PCONTEXT context, int max_frames, void** out_frames);

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

	MINIDUMP_USER_STREAM userStream = {};
	userStream.Type = UserStackFrameListStream;
	userStream.BufferSize = sizeof(StackFrameList);
	userStream.Buffer = &dump_params->stack_frames;

	MINIDUMP_USER_STREAM_INFORMATION userStreamList = {};
	userStreamList.UserStreamCount = 1;
	userStreamList.UserStreamArray = &userStream;

	MINIDUMP_EXCEPTION_INFORMATION info;
	info.ThreadId = dump_params->thread_id;
	info.ExceptionPointers = dump_params->exception_pointers;
	info.ClientPointers = 0;
	MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
		MiniDumpWithHandleData |
		MiniDumpWithProcessThreadData |
		MiniDumpWithFullMemoryInfo |
		MiniDumpWithThreadInfo);
	func_MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &info, &userStreamList, 0);
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
	GetLog(writeFile);
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

int CrashReporter::CaptureStackTrace(PCONTEXT initcontext, int max_frames, void** out_frames)
{
	memset(out_frames, 0, sizeof(void*) * max_frames);

#ifdef _WIN64
	// RtlCaptureStackBackTrace doesn't support RtlAddFunctionTable..

	CONTEXT context;
	if (initcontext)
		memcpy(&context, initcontext, sizeof(CONTEXT));
	else
		RtlCaptureContext(&context);

	UNWIND_HISTORY_TABLE history;
	memset(&history, 0, sizeof(UNWIND_HISTORY_TABLE));

	ULONG64 establisherframe = 0;
	PVOID handlerdata = nullptr;

	int frame = 0;

	// If the context came from an exception then we want its location included too
	// Otherwise first frame is the call to this function which we never want to see
	if (initcontext && frame < max_frames)
		out_frames[frame++] = (void*)context.Rip;

	while (frame < max_frames)
	{
		ULONG64 imagebase;
		PRUNTIME_FUNCTION rtfunc = RtlLookupFunctionEntry(context.Rip, &imagebase, &history);

		KNONVOLATILE_CONTEXT_POINTERS nvcontext;
		memset(&nvcontext, 0, sizeof(KNONVOLATILE_CONTEXT_POINTERS));
		if (!rtfunc)
		{
			// Leaf function
			context.Rip = (ULONG64)(*(PULONG64)context.Rsp);
			context.Rsp += 8;
		}
		else
		{
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, imagebase, context.Rip, rtfunc, &context, &handlerdata, &establisherframe, &nvcontext);
		}

		if (!context.Rip)
			break;

		out_frames[frame] = (void*)context.Rip;
		frame++;
	}
	return frame;
#else
	return 0;
#endif
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
	dump_params.stack_frames.frame_count = CaptureStackTrace(exception_pointers->ContextRecord, StackFrameList::max_frames, dump_params.stack_frames.frames);

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
	dump_params.stack_frames.frame_count = CaptureStackTrace(exception_pointers->ContextRecord, StackFrameList::max_frames, dump_params.stack_frames.frames);

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

static std::wstring GetExeFilename()
{
	wchar_t buffer[1024] = {};
	if (GetModuleFileName(0, buffer, 1024) == FALSE)
		return {};
	return buffer;
}

static std::wstring GetExePath()
{
	std::wstring exePath = GetExeFilename();
	size_t pos = exePath.find_last_of(L"/\\");
	if (pos == std::wstring::npos)
		return {};
	exePath.resize(pos);
	return exePath;
}

static std::wstring GetPdbFilename()
{
	std::wstring pdbFilename = GetExeFilename();
	pdbFilename.resize(pdbFilename.size() - 3);
	pdbFilename += L"pdb";
	return pdbFilename;
}

static bool IsPdbFileMissing()
{
	std::wstring pdbFilename = GetPdbFilename();
	HANDLE filehandle = CreateFile(pdbFilename.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (filehandle == INVALID_HANDLE_VALUE)
		return true;
	CloseHandle(filehandle);
	return false;
}

#if defined(USE_DBGENG)

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
	if (IsPdbFileMissing())
		logText.AppendFormat("\nWarning: could not find " GAMENAMELOWERCASE ".pdb - No call stack will be displayed for the crash.\n");

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

	symbols->SetSymbolOptions(
		SYMOPT_CASE_INSENSITIVE |
		SYMOPT_UNDNAME |
		SYMOPT_DEFERRED_LOADS |
		SYMOPT_LOAD_LINES |
		SYMOPT_OMAP_FIND_NEAREST |
		SYMOPT_FAIL_CRITICAL_ERRORS |
		SYMOPT_NO_PROMPTS);
	symbols->SetImagePathWide(GetExePath().c_str());
	symbols->SetSymbolPathWide(GetExePath().c_str());

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
		uselessInfoPos = description.find(" - code");
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

#else

struct MinidumpStream
{
	MINIDUMP_DIRECTORY* Directory = nullptr;
	void* Data = nullptr;
	ULONG Size = 0;
};

class MinidumpFile
{
public:
	MinidumpFile(const wchar_t* filename)
	{
		FileHandle = CreateFile(filename, FILE_GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, 0);
		if (!FileHandle)
			I_FatalError("Could not open minidump file");

		MappingHandle = CreateFileMapping(FileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
		if (!MappingHandle)
		{
			CloseHandle(FileHandle);
			I_FatalError("Could not create minidump file mapping");
		}

		BaseOfDump = MapViewOfFile(MappingHandle, FILE_MAP_READ, 0, 0, 0);
		if (!BaseOfDump)
		{
			CloseHandle(MappingHandle);
			CloseHandle(FileHandle);
			I_FatalError("Could not map minidump file");
		}
	}

	~MinidumpFile()
	{
		UnmapViewOfFile(BaseOfDump);
		CloseHandle(MappingHandle);
		CloseHandle(FileHandle);
	}

	std::vector<MINIDUMP_THREAD> GetThreadList()
	{
		MinidumpStream s = ReadStream(ThreadListStream);
		MINIDUMP_THREAD_LIST* list = static_cast<MINIDUMP_THREAD_LIST*>(s.Data);
		std::vector<MINIDUMP_THREAD> threads(list->NumberOfThreads);
		for (size_t i = 0; i < threads.size(); i++)
			threads[i] = list->Threads[i];
		return threads;
	}

	std::vector<MINIDUMP_THREAD_EX> GetThreadExList()
	{
		MinidumpStream s = ReadStream(ThreadExListStream);
		MINIDUMP_THREAD_EX_LIST* list = static_cast<MINIDUMP_THREAD_EX_LIST*>(s.Data);
		std::vector<MINIDUMP_THREAD_EX> threads(list->NumberOfThreads);
		for (size_t i = 0; i < threads.size(); i++)
			threads[i] = list->Threads[i];
		return threads;
	}

	std::vector<MINIDUMP_MODULE> GetModuleList()
	{
		MinidumpStream s = ReadStream(ModuleListStream);
		MINIDUMP_MODULE_LIST* list = static_cast<MINIDUMP_MODULE_LIST*>(s.Data);
		std::vector<MINIDUMP_MODULE> modules(list->NumberOfModules);
		for (size_t i = 0; i < modules.size(); i++)
			modules[i] = list->Modules[i];
		return modules;
	}

	std::vector<MINIDUMP_MEMORY_DESCRIPTOR> GetMemoryList()
	{
		MinidumpStream s = ReadStream(MemoryListStream);
		MINIDUMP_MEMORY_LIST* list = static_cast<MINIDUMP_MEMORY_LIST*>(s.Data);
		std::vector<MINIDUMP_MEMORY_DESCRIPTOR> descriptors(list->NumberOfMemoryRanges);
		for (size_t i = 0; i < descriptors.size(); i++)
			descriptors[i] = list->MemoryRanges[i];
		return descriptors;
	}

	std::vector<MINIDUMP_MEMORY_DESCRIPTOR64> GetMemory64List()
	{
		MinidumpStream s = ReadStream(Memory64ListStream);
		MINIDUMP_MEMORY64_LIST* list = static_cast<MINIDUMP_MEMORY64_LIST*>(s.Data);
		std::vector<MINIDUMP_MEMORY_DESCRIPTOR64> descriptors(list->NumberOfMemoryRanges);
		for (size_t i = 0; i < descriptors.size(); i++)
			descriptors[i] = list->MemoryRanges[i];
		return descriptors;
	}

	StackFrameList* GetUserStackFrameList()
	{
		MinidumpStream s = ReadStream(UserStackFrameListStream);
		if (s.Size == sizeof(StackFrameList))
			return static_cast<StackFrameList*>(s.Data);
		else
			return nullptr;
	}

	MINIDUMP_EXCEPTION_STREAM GetException()
	{
		MinidumpStream s = ReadStream(ExceptionStream);
		return *static_cast<MINIDUMP_EXCEPTION_STREAM*>(s.Data);
	}

	MINIDUMP_SYSTEM_INFO GetSystemInfo()
	{
		MinidumpStream s = ReadStream(SystemInfoStream);
		return *static_cast<MINIDUMP_SYSTEM_INFO*>(s.Data);
	}

	MinidumpStream ReadStream(ULONG streamNumber)
	{
		MinidumpStream stream;
		BOOL result = MiniDumpReadDumpStream(BaseOfDump, streamNumber, &stream.Directory, &stream.Data, &stream.Size);
		if (!result)
			I_FatalError("MiniDumpReadDumpStream failed");
		return stream;
	}

	template<typename T>
	T* GetPtr(RVA rva)
	{
		return reinterpret_cast<T*>(static_cast<char*>(BaseOfDump) + rva);
	}

	std::wstring GetString(RVA rva)
	{
		MINIDUMP_STRING* str = GetPtr<MINIDUMP_STRING>(rva);
		return std::wstring(str->Buffer, str->Length);
	}

	HANDLE FileHandle = {};
	HANDLE MappingHandle = {};
	void* BaseOfDump = nullptr;

private:
	MinidumpFile(const MinidumpFile&) = delete;
	MinidumpFile& operator=(const MinidumpFile&) = delete;
};

class MinidumpDebugger
{
public:
	MinidumpDebugger()
	{
		// We need an unique legal process handle for our debug session. Yes, this is really how Microsoft designed this API!
		HANDLE currentProcess = GetCurrentProcess();
		BOOL result = DuplicateHandle(currentProcess, currentProcess, currentProcess, &process, 0, FALSE, DUPLICATE_SAME_ACCESS);
		if (!result)
			I_FatalError("DuplicateHandle failed");

		DWORD flags =
			SYMOPT_UNDNAME |               // Return undecorated symbol names
			SYMOPT_DEFERRED_LOADS |        // Delay loading of modules and pdb files until we need them
			SYMOPT_EXACT_SYMBOLS |         // Only load modules that match the minidump
			SYMOPT_FAIL_CRITICAL_ERRORS |  // Never show any UI dialogs to the user
			SYMOPT_LOAD_LINES;             // Load symbol line info
		SymSetOptions(flags);

		result = SymInitialize(process, nullptr, FALSE);
		if (!result)
		{
			CloseHandle(process);
			I_FatalError("SymInitialize failed");
		}
	}

	~MinidumpDebugger()
	{
		for (auto& module : Modules)
		{
			if (module.BaseOfDll != 0)
				SymUnloadModule64(process, module.BaseOfDll);
		}

		SymCleanup(process);
		CloseHandle(process);
	}

	void Open(const wchar_t* filename)
	{
		minidump = std::make_unique<MinidumpFile>(filename);
		LoadModuleList();
		LoadExceptionInfo();
		LoadUserStackFrameList();
	}

	const StackFrameList& GetStackFrameList() const { return stackFrameList; }

	FString GetExceptionText()
	{
		if (ExceptionInfo.Record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ExceptionInfo.Record.NumberParameters >= 2)
		{
			ULONG64 readWriteFlag = ExceptionInfo.Record.ExceptionInformation[0];
			ULONG64 addr = ExceptionInfo.Record.ExceptionInformation[1];

			FString addressValue;
			addressValue.Format("0x%04x%04x%04x%04x", (int)((addr >> 48) & 0xffff), (int)((addr >> 32) & 0xffff), (int)((addr >> 16) & 0xffff), (int)(addr & 0xffff));

			if (readWriteFlag == 0)
				return "Access violation reading from " + addressValue;
			else if (readWriteFlag == 1)
				return "Access violation writing to " + addressValue;
		}

		switch (ExceptionInfo.Record.ExceptionCode)
		{
		case EXCEPTION_ACCESS_VIOLATION: return "Access violation";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "Array bounds exceeded";
		case EXCEPTION_BREAKPOINT: return "Breakpoint";
		case EXCEPTION_DATATYPE_MISALIGNMENT: return "Datatype misalignment";
		case EXCEPTION_FLT_DENORMAL_OPERAND: return "Floating point denormal operand";
		case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "Floating point divide by zero";
		case EXCEPTION_FLT_INEXACT_RESULT: return "Floating point inexact result";
		case EXCEPTION_FLT_INVALID_OPERATION: return "Floating point invalid operation";
		case EXCEPTION_FLT_OVERFLOW: return "Floating point overflow";
		case EXCEPTION_FLT_STACK_CHECK: return "Floating point stack check";
		case EXCEPTION_FLT_UNDERFLOW: return "Floating point underflow";
		case EXCEPTION_ILLEGAL_INSTRUCTION: return "Floating point illegal instruction";
		case EXCEPTION_IN_PAGE_ERROR: return "In page error";
		case EXCEPTION_INT_DIVIDE_BY_ZERO: return "Integer divide by zero";
		case EXCEPTION_INT_OVERFLOW: return "Integer overflow";
		case EXCEPTION_INVALID_DISPOSITION: return "Invalid disposition";
		case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "Noncontinuable exception";
		case EXCEPTION_PRIV_INSTRUCTION: return "Priv instruction";
		case EXCEPTION_SINGLE_STEP: return "Single step";
		case EXCEPTION_STACK_OVERFLOW: return "Stack overflow";
		default: return {};
		}
	}

	FString GetModuleName(void* frame)
	{
		IMAGEHLP_MODULE64 moduleInfo = { sizeof(IMAGEHLP_MODULE64) };
		SymGetModuleInfo64(process, (DWORD64)frame, &moduleInfo);
		return moduleInfo.ModuleName;
	}

	FString GetCalledFromText(void* frame)
	{
		FString s;

		unsigned char buffer[sizeof(IMAGEHLP_SYMBOL64) + 128];
		IMAGEHLP_SYMBOL64* symbol64 = reinterpret_cast<IMAGEHLP_SYMBOL64*>(buffer);
		memset(symbol64, 0, sizeof(IMAGEHLP_SYMBOL64) + 128);
		symbol64->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
		symbol64->MaxNameLength = 128;

		DWORD64 displacement = 0;
		BOOL result = SymGetSymFromAddr64(process, (DWORD64)frame, &displacement, symbol64);
		if (result)
		{
			IMAGEHLP_LINE64 line64;
			DWORD displacement1 = 0;
			memset(&line64, 0, sizeof(IMAGEHLP_LINE64));
			line64.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
			result = SymGetLineFromAddr64(process, (DWORD64)frame, &displacement1, &line64);
			if (result)
			{
				s.Format("Called from %s at %s, line %d\n", symbol64->Name, line64.FileName, (int)line64.LineNumber);
			}
			else
			{
				s.Format("Called from %s\n", symbol64->Name);
			}
		}

		return s;
	}

private:
	struct Module
	{
		std::string ImageName;
		DWORD64 BaseOfDll = 0;
	};

	std::vector<Module> Modules;

	struct
	{
		int ThreadId = 0;
		CONTEXT* Context = nullptr;
		MINIDUMP_EXCEPTION Record = {};
	} ExceptionInfo;

	void LoadModuleList()
	{
		for (const MINIDUMP_MODULE& module : minidump->GetModuleList())
		{
			Module m;
			m.ImageName = from_utf16(minidump->GetString(module.ModuleNameRva));
			m.BaseOfDll = SymLoadModuleEx(process, 0, m.ImageName.c_str(), nullptr, module.BaseOfImage, module.SizeOfImage, nullptr, 0);
			Modules.push_back(std::move(m));
		}
	}

	void LoadExceptionInfo()
	{
		auto info = minidump->GetException();
		ExceptionInfo.ThreadId = info.ThreadId;
		if (info.ThreadContext.DataSize == sizeof(CONTEXT))
			ExceptionInfo.Context = minidump->GetPtr<CONTEXT>(info.ThreadContext.Rva);
		ExceptionInfo.Record = info.ExceptionRecord;
	}

	void LoadUserStackFrameList()
	{
		StackFrameList* list = minidump->GetUserStackFrameList();
		if (list)
			stackFrameList = *list;
	}

	static std::string from_utf16(const std::wstring& str)
	{
		if (str.empty()) return {};
		int needed = WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0, nullptr, nullptr);
		if (needed == 0)
			I_FatalError("WideCharToMultiByte failed");
		std::string result;
		result.resize(needed);
		needed = WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), &result[0], (int)result.size(), nullptr, nullptr);
		if (needed == 0)
			I_FatalError("WideCharToMultiByte failed");
		return result;
	}

	std::unique_ptr<MinidumpFile> minidump;
	HANDLE process = 0;
	StackFrameList stackFrameList = {};
};

void I_AddMinidumpCallstack(const FString& minidumpFilename, FString& text, FString& logText)
{
	if (IsPdbFileMissing())
		logText.AppendFormat("\nWarning: could not find " GAMENAMELOWERCASE ".pdb - No call stack will be displayed for the crash.\n");

	try
	{
		MinidumpDebugger debugger;
		debugger.Open(minidumpFilename.WideString().c_str());

		FString exceptionText = debugger.GetExceptionText();
		if (!exceptionText.IsEmpty())
		{
			text = exceptionText;
			logText.AppendFormat("\n%s\n", exceptionText.GetChars());
		}

		FString lastExternalCode;
		const StackFrameList& callstack = debugger.GetStackFrameList();
		for (int i = 0; i < callstack.frame_count; i++)
		{
			FString text = debugger.GetCalledFromText(callstack.frames[i]);
			if (!text.IsEmpty())
			{
				logText += text;
				lastExternalCode = "";
			}
			else
			{
				FString moduleName = debugger.GetModuleName(callstack.frames[i]);
				if (!moduleName.IsEmpty() && moduleName != lastExternalCode)
					logText.AppendFormat("Called from external code [%s]\n", moduleName.GetChars());
				lastExternalCode = moduleName;
			}
		}
	}
	catch (...)
	{
		// Just ignore errors for now.
	}
}

#endif
