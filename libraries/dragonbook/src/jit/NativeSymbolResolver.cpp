
#include "NativeSymbolResolver.h"

#ifdef WIN32
#include <Windows.h>
#include <DbgHelp.h>
#else
#include <execinfo.h>
#include <cxxabi.h>
#include <cstring>
#include <cstdlib>
#include <memory>
#endif

#ifdef WIN32

#pragma comment(lib, "dbghelp.lib")

NativeSymbolResolver::NativeSymbolResolver()
{
	SymInitialize(GetCurrentProcess(), nullptr, TRUE);
}

NativeSymbolResolver::~NativeSymbolResolver()
{
	SymCleanup(GetCurrentProcess());
}

JITStackFrame NativeSymbolResolver::GetName(void* frame)
{
	JITStackFrame s;

	unsigned char buffer[sizeof(IMAGEHLP_SYMBOL64) + 128];
	IMAGEHLP_SYMBOL64* symbol64 = reinterpret_cast<IMAGEHLP_SYMBOL64*>(buffer);
	memset(symbol64, 0, sizeof(IMAGEHLP_SYMBOL64) + 128);
	symbol64->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
	symbol64->MaxNameLength = 128;

	DWORD64 displacement = 0;
	BOOL result = SymGetSymFromAddr64(GetCurrentProcess(), (DWORD64)frame, &displacement, symbol64);
	if (result)
	{
		s.PrintableName = symbol64->Name;

		IMAGEHLP_LINE64 line64;
		DWORD displacement = 0;
		memset(&line64, 0, sizeof(IMAGEHLP_LINE64));
		line64.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
		result = SymGetLineFromAddr64(GetCurrentProcess(), (DWORD64)frame, &displacement, &line64);
		if (result)
		{
			s.FileName = line64.FileName;
			s.LineNumber = (int)line64.LineNumber;
		}
	}

	return s;
}

#else

NativeSymbolResolver::NativeSymbolResolver()
{
}

NativeSymbolResolver::~NativeSymbolResolver()
{
}

JITStackFrame NativeSymbolResolver::GetName(void* frame)
{
	JITStackFrame s;
	char** strings;
	void* frames[1] = { frame };
	strings = backtrace_symbols(frames, 1);

	// Decode the strings
	char* ptr = strings[0];
	char* filename = ptr;
	const char* function = "";

	// Find function name
	while (*ptr)
	{
		if (*ptr == '(')	// Found function name
		{
			*(ptr++) = 0;
			function = ptr;
			break;
		}
		ptr++;
	}

	// Find offset
	if (function[0])	// Only if function was found
	{
		while (*ptr)
		{
			if (*ptr == '+')	// Found function offset
			{
				*(ptr++) = 0;
				break;
			}
			if (*ptr == ')')	// Not found function offset, but found, end of function
			{
				*(ptr++) = 0;
				break;
			}
			ptr++;
		}
	}

	int status;
	char* new_function = abi::__cxa_demangle(function, nullptr, nullptr, &status);
	if (new_function)	// Was correctly decoded
	{
		function = new_function;
	}

	s.PrintableName = function;
	s.FileName = filename;

	if (new_function)
	{
		free(new_function);
	}

	free(strings);
	return s;
}

#endif
