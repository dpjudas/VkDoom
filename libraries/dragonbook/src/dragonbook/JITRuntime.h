#pragma once

#include <map>
#include <vector>
#include <string>

class IRContext;
class IRFunction;
class IRValue;
class IRConstant;
class IRGlobalVariable;
class MachineCodeHolder;
class NativeSymbolResolver;

class JITStackFrame
{
public:
	std::string PrintableName;
	std::string FileName;
	int LineNumber = -1;

	explicit operator bool() const { return !PrintableName.empty() || !FileName.empty() || LineNumber != -1; }
};

class JITRuntime
{
public:
	JITRuntime();
	~JITRuntime();

	void add(IRContext* context);

	void* getPointerToFunction(const std::string& func);
	void* getPointerToGlobal(const std::string& variable);

	std::vector<JITStackFrame> captureStackTrace(int framesToSkip, bool includeNativeFrames);

private:
	void initGlobal(int offset, IRConstant* value);

	void add(MachineCodeHolder* codeholder);
	void* allocJitMemory(size_t size);
	void* virtualAlloc(size_t size);
	void virtualFree(void* ptr);

	JITStackFrame getStackFrame(NativeSymbolResolver* nativeSymbols, void* frame);

	void addDebugInfo(uint8_t* baseaddr, MachineCodeHolder* codeholder);

	std::map<std::string, void*> functionTable;
	std::map<std::string, size_t> globalTable;
	std::vector<uint8_t> globals;

	std::vector<uint8_t*> blocks;
	std::vector<uint8_t*> frames;
	size_t blockPos = 0;
	size_t blockSize = 0;

	struct JitInstInfo
	{
		void* offset = nullptr;
		int fileIndex = -1;
		int lineNumber = -1;
	};

	struct JitFuncInfo
	{
		std::string printableName;
		void* startAddr = nullptr;
		void* endAddr = nullptr;
		std::vector<JitInstInfo> instructions;
		std::vector<std::string> files;
	};
	std::vector<JitFuncInfo> debugInfo;
	std::vector<std::string> debugFilenames;
};
