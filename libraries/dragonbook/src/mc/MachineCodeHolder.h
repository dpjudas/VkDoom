#pragma once

#include "dragonbook/IR.h"
#include "MachineInst.h"

class MachineCodeHolder
{
public:
	void addFunction(IRFunction* func);
	void addFunction(IRFunction* func, const void* external);

	size_t codeSize() const { return code.size(); }
	size_t dataSize() const { return data.size(); }
	size_t unwindDataSize() const { return unwindData.size(); }

	void relocate(void* codeDest, void* dataDest, void* unwindDest) const;

	struct FunctionEntry
	{
		IRFunction* func = nullptr;
		const void* external = nullptr;

		size_t beginAddress = 0;
		size_t endAddress = 0;

		size_t beginUnwindData = 0;
		size_t endUnwindData = 0;
		unsigned int unixUnwindFunctionStart = 0;
	};

	const std::vector<FunctionEntry>& getFunctionTable() const { return functionTable; };

	struct DebugInfo
	{
		size_t offset = 0;
		int fileIndex = -1;
		int lineNumber = -1;
	};
	const std::vector<DebugInfo>& getDebugInfo() const { return debugInfo; }
	const std::vector<std::string>& getFileInfo() const { return fileInfo; }

private:
	std::vector<DebugInfo> debugInfo;
	std::vector<std::string> fileInfo;
	std::vector<uint8_t> code;
	std::vector<uint8_t> data;
	std::vector<uint8_t> unwindData;
	std::vector<FunctionEntry> functionTable;
	std::map<IRFunction*, size_t> funcToTableIndex;

	struct BBRelocateEntry
	{
		BBRelocateEntry() = default;
		BBRelocateEntry(size_t pos, MachineBasicBlock* bb) : pos(pos), bb(bb) { }

		size_t pos = 0;
		MachineBasicBlock* bb = nullptr;
	};

	struct CallRelocateEntry
	{
		CallRelocateEntry() = default;
		CallRelocateEntry(size_t pos, IRFunction* func) : pos(pos), func(func) { }

		size_t pos = 0;
		IRFunction* func = nullptr;
	};

	struct DataRelocateEntry
	{
		DataRelocateEntry() = default;
		DataRelocateEntry(size_t codepos, size_t datapos) : codepos(codepos), datapos(datapos) { }

		size_t codepos = 0;
		size_t datapos = 0;
	};

	std::vector<DataRelocateEntry> dataRelocateInfo;
	std::vector<CallRelocateEntry> callRelocateInfo;
	std::vector<BBRelocateEntry> bbRelocateInfo;
	std::map<MachineBasicBlock*, size_t> bbOffsets;

	friend class MachineCodeWriter;
};
