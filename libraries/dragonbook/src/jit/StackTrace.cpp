
#include "StackTrace.h"

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

#if WIN32

int StackTrace::Capture(int max_frames, void** out_frames)
{
	memset(out_frames, 0, sizeof(void*) * max_frames);

#ifdef _WIN64
	// RtlCaptureStackBackTrace doesn't support RtlAddFunctionTable..

	CONTEXT context;
	RtlCaptureContext(&context);

	UNWIND_HISTORY_TABLE history;
	memset(&history, 0, sizeof(UNWIND_HISTORY_TABLE));

	ULONG64 establisherframe = 0;
	PVOID handlerdata = nullptr;

	int frame;
	for (frame = 0; frame < max_frames; frame++)
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
	}
	return frame;

#else
	return 0; //return RtlCaptureStackBackTrace(0, MIN(max_frames, 32), out_frames, nullptr);
#endif
}

#else

int StackTrace::Capture(int max_frames, void** out_frames)
{
	memset(out_frames, 0, sizeof(void*) * max_frames);
	return backtrace(out_frames, max_frames);
}

#endif
