
#include "dragonbook/JITRuntime.h"
#include "StackTrace.h"
#include "NativeSymbolResolver.h"
#include "mc/MachineCodeHolder.h"
#include <memory>

#ifdef WIN32
#include <Windows.h>
#endif

#ifndef WIN32
#include <sys/mman.h>

extern "C"
{
	void __register_frame(const void*);
	void __deregister_frame(const void*);
}
#endif

JITRuntime::JITRuntime()
{
}

JITRuntime::~JITRuntime()
{
#ifdef _WIN64
	for (auto p : frames)
	{
		RtlDeleteFunctionTable((PRUNTIME_FUNCTION)p);
	}
#elif !defined(WIN32)
	for (auto p : frames)
	{
		__deregister_frame(p);
	}
#endif

	for (uint8_t* block : blocks)
	{
		virtualFree(block);
	}
}

void JITRuntime::add(IRContext* context)
{
	auto& functions = context->functions;
	auto& variables = context->globalVars;
	auto& globalMappings = context->globalMappings;

	size_t globalsSize = globals.size();
	for (const auto& it : variables)
	{
		IRGlobalVariable* var = it.second;

		int size = (var->type->getPointerElementType()->getTypeAllocSize() + 7) / 8 * 8;
		var->globalsOffset = (int)globalsSize;
		globalsSize += size;

		globalTable[var->name] = var->globalsOffset;
	}

	globals.resize(globalsSize);

	MachineCodeHolder codeholder;

	for (const auto& it : functions)
	{
		IRFunction* func = it.second;
		codeholder.addFunction(func);
	}

	for (const auto& it : globalMappings)
	{
		IRFunction* func = dynamic_cast<IRFunction*>(it.first);
		if (func)
		{
			codeholder.addFunction(func, it.second);
		}
	}

	add(&codeholder);

	for (const auto& it : variables)
	{
		IRGlobalVariable* var = it.second;
		if (var->initialValue)
		{
			initGlobal(var->globalsOffset, var->initialValue);
		}
	}
}

void JITRuntime::initGlobal(int offset, IRConstant* value)
{
	IRConstantStruct* initStruct = dynamic_cast<IRConstantStruct*>(value);
	IRConstantInt* initInt = dynamic_cast<IRConstantInt*>(value);
	IRConstantFP* initFloat = dynamic_cast<IRConstantFP*>(value);
	IRFunction* initFunc = dynamic_cast<IRFunction*>(value);

	if (initStruct)
	{
		for (IRConstant* svalue : initStruct->values)
		{
			initGlobal(offset, svalue);
			int size = (svalue->type->getTypeAllocSize() + 7) / 8 * 8;
			offset += size;
		}
	}
	else if (initInt)
	{
		if (dynamic_cast<IRInt32Type*>(initInt->type) || dynamic_cast<IRInt1Type*>(initInt->type))
		{
			*reinterpret_cast<uint32_t*>(globals.data() + offset) = (uint32_t)initInt->value;
		}
		else if (dynamic_cast<IRInt64Type*>(initInt->type))
		{
			*reinterpret_cast<uint64_t*>(globals.data() + offset) = (uint64_t)initInt->value;
		}
		else if (dynamic_cast<IRInt16Type*>(initInt->type))
		{
			*reinterpret_cast<uint16_t*>(globals.data() + offset) = (uint16_t)initInt->value;
		}
		else if (dynamic_cast<IRInt8Type*>(initInt->type))
		{
			*reinterpret_cast<uint8_t*>(globals.data() + offset) = (uint8_t)initInt->value;
		}
		else
		{
			throw std::runtime_error("Unknown IRConstantInt type");
		}
	}
	else if (initFloat)
	{
		if (dynamic_cast<IRFloatType*>(initFloat->type))
		{
			*reinterpret_cast<float*>(globals.data() + offset) = (float)initFloat->value;
		}
		else if (dynamic_cast<IRDoubleType*>(initFloat->type))
		{
			*reinterpret_cast<double*>(globals.data() + offset) = initFloat->value;
		}
		else
		{
			throw std::runtime_error("Unknown IRConstantFP type");
		}
	}
	else if (initFunc)
	{
		*reinterpret_cast<void**>(globals.data() + offset) = getPointerToFunction(initFunc->name);
	}
	else
	{
		throw std::runtime_error("Unknown IRConstant type");
	}
}

void* JITRuntime::getPointerToFunction(const std::string& func)
{
	auto it = functionTable.find(func);
	if (it != functionTable.end())
		return it->second;
	else
		return nullptr;
}

void* JITRuntime::getPointerToGlobal(const std::string& var)
{
	auto it = globalTable.find(var);
	if (it != globalTable.end())
		return globals.data() + it->second;
	else
		return nullptr;
}

#ifdef WIN32

void JITRuntime::add(MachineCodeHolder* codeholder)
{
#ifdef _WIN64
	size_t codeSize = codeholder->codeSize();
	size_t dataSize = codeholder->dataSize();
	size_t unwindDataSize = codeholder->unwindDataSize();
	size_t functionTableSize = codeholder->getFunctionTable().size() * sizeof(RUNTIME_FUNCTION);

	// Make sure everything is 16 byte aligned
	codeSize = (codeSize + 15) / 16 * 16;
	dataSize = (dataSize + 15) / 16 * 16;
	unwindDataSize = (unwindDataSize + 15) / 16 * 16;
	functionTableSize = (functionTableSize + 15) / 16 * 16;

	uint8_t* baseaddr = (uint8_t*)allocJitMemory(codeSize + dataSize + unwindDataSize + functionTableSize);
	uint8_t* dataaddr = baseaddr + codeSize;
	uint8_t* unwindaddr = baseaddr + codeSize + dataSize;
	uint8_t* tableaddr = baseaddr + codeSize + dataSize + unwindDataSize;

	codeholder->relocate(baseaddr, dataaddr, unwindaddr);

	RUNTIME_FUNCTION* table = (RUNTIME_FUNCTION*)tableaddr;
	int tableIndex = 0;
	for (const auto& entry : codeholder->getFunctionTable())
	{
		if (!entry.external)
		{
			table[tableIndex].BeginAddress = (DWORD)entry.beginAddress;
			table[tableIndex].EndAddress = (DWORD)entry.endAddress;
#ifndef __MINGW64__
			table[tableIndex].UnwindInfoAddress = (DWORD)(codeSize + dataSize + entry.beginUnwindData);
#else
			table[tableIndex].UnwindData = (DWORD)(codeSize + dataSize + entry.beginUnwindData);
#endif
			tableIndex++;
		}
	}

	BOOLEAN result = RtlAddFunctionTable(table, (DWORD)tableIndex, (DWORD64)baseaddr);
	frames.push_back((uint8_t*)table);
	if (result == 0)
		throw std::runtime_error("RtlAddFunctionTable failed");

	for (const auto& entry : codeholder->getFunctionTable())
	{
		if (!entry.external)
		{
			functionTable[entry.func->name] = baseaddr + entry.beginAddress;
		}
		else
		{
			functionTable[entry.func->name] = (void*)entry.external;
		}
	}
#endif
}

void* JITRuntime::virtualAlloc(size_t size)
{
	return VirtualAllocEx(GetCurrentProcess(), nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

void JITRuntime::virtualFree(void* ptr)
{
	VirtualFreeEx(GetCurrentProcess(), ptr, 0, MEM_RELEASE);
}

#else

void JITRuntime::add(MachineCodeHolder* codeholder)
{
	size_t codeSize = codeholder->codeSize();
	size_t dataSize = codeholder->dataSize();
	size_t unwindDataSize = codeholder->unwindDataSize();

	// Make sure everything is 16 byte aligned
	codeSize = (codeSize + 15) / 16 * 16;
	dataSize = (dataSize + 15) / 16 * 16;
	unwindDataSize = (unwindDataSize + 15) / 16 * 16;

	uint8_t* baseaddr = (uint8_t*)allocJitMemory(codeSize + dataSize + unwindDataSize);
	uint8_t* dataaddr = baseaddr + codeSize;
	uint8_t* unwindaddr = baseaddr + codeSize + dataSize;

	codeholder->relocate(baseaddr, dataaddr, unwindaddr);

	if (unwindDataSize > 0)
	{
#ifdef __APPLE__
		// On macOS __register_frame takes a single FDE as an argument
		uint8_t* entry = unwindaddr;
		while (true)
		{
			uint32_t length = *((uint32_t*)entry);
			if (length == 0)
				break;

			if (length == 0xffffffff)
			{
				uint64_t length64 = *((uint64_t*)(entry + 4));
				if (length64 == 0)
					break;

				uint64_t offset = *((uint64_t*)(entry + 12));
				if (offset != 0)
				{
					__register_frame(entry);
					frames.push_back(entry);
				}
				entry += length64 + 12;
			}
			else
			{
				uint32_t offset = *((uint32_t*)(entry + 4));
				if (offset != 0)
				{
					__register_frame(entry);
					frames.push_back(entry);
				}
				entry += length + 4;
			}
		}
#else
		// On Linux it takes a pointer to the entire .eh_frame
		__register_frame(unwindaddr);
		frames.push_back(unwindaddr);
#endif
	}

	for (const auto& entry : codeholder->getFunctionTable())
	{
		if (!entry.external)
		{
			functionTable[entry.func->name] = baseaddr + entry.beginAddress;
		}
		else
		{
			functionTable[entry.func->name] = (void*)entry.external;
		}
	}
}

void* JITRuntime::virtualAlloc(size_t size)
{
	void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED) return nullptr;
	return ptr;
}

void JITRuntime::virtualFree(void* ptr)
{
	munmap(ptr, blockSize);
}

#endif

void* JITRuntime::allocJitMemory(size_t size)
{
	if (blockPos + size <= blockSize)
	{
		uint8_t* p = blocks[blocks.size() - 1];
		p += blockPos;
		blockPos += size;
		return p;
	}
	else
	{
		size_t allocSize = 1024 * 1024;
		void* base = virtualAlloc(allocSize);
		if (!base)
			throw std::runtime_error("VirtualAllocEx failed");

		blocks.push_back((uint8_t*)base);
		blockSize = allocSize;
		blockPos = size;
		return base;
	}
}

std::vector<JITStackFrame> JITRuntime::captureStackTrace(int framesToSkip, bool includeNativeFrames)
{
	void* frames[32];
	int numframes = StackTrace::Capture(32, frames);

	std::unique_ptr<NativeSymbolResolver> nativeSymbols;
	if (includeNativeFrames)
		nativeSymbols.reset(new NativeSymbolResolver());

	std::vector<JITStackFrame> jitframes;
	for (int i = framesToSkip + 1; i < numframes; i++)
	{
		JITStackFrame frame = getStackFrame(nativeSymbols.get(), frames[i]);
		if (frame)
			jitframes.push_back(frame);
	}
	return jitframes;
}

JITStackFrame JITRuntime::getStackFrame(NativeSymbolResolver* nativeSymbols, void* pc)
{
	/*for (unsigned int i = 0; i < JitDebugInfo.Size(); i++)
	{
		const auto& info = JitDebugInfo[i];
		if (pc >= info.start && pc < info.end)
		{
			return PCToStackFrameInfo((uint8_t*)pc, &info);
		}
	}*/

	return nativeSymbols ? nativeSymbols->GetName(pc) : JITStackFrame();
}
/*
JITStackFrame PCToStackFrameInfo(uint8_t* pc, const JitFuncInfo* info)
{
	int PCIndex = int(pc - ((uint8_t*)(info->start)));
	if (info->LineInfo.Size() == 1) return info->LineInfo[0].LineNumber;
	for (unsigned i = 1; i < info->LineInfo.Size(); i++)
	{
		if (info->LineInfo[i].InstructionIndex >= PCIndex)
		{
			return info->LineInfo[i - 1].LineNumber;
		}
	}
	return -1;
}
*/
