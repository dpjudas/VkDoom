#pragma once

#include "MachineInst.h"
#include <list>
#include <set>

class RARegisterLiveReference
{
public:
	RARegisterLiveReference(MachineBasicBlock* bb) : bb(bb) { }

	MachineBasicBlock* bb = nullptr;
	int refcount = 1;
	std::unique_ptr<RARegisterLiveReference> next;
};

struct RARegisterInfo
{
	static MachineOperand nullStackLocation() { MachineOperand op; op.type = MachineOperandType::spillOffset; op.spillOffset = -1; return op; }

	MachineRegClass cls = MachineRegClass::reserved;
	int vreg = -1;
	int physreg = -1;
	MachineOperand stacklocation = nullStackLocation();
	bool modified = false;
	bool stackvar = false;
	const std::string* name = nullptr;

	std::unique_ptr<RARegisterLiveReference> liveReferences;
};

struct RARegisterClass
{
	std::list<int> mru;
	std::list<int>::iterator mruIt[(int)RegisterName::vregstart];
};

class RegisterAllocator
{
public:
	static void run(IRContext* context, MachineFunction* func);

private:
	RegisterAllocator(IRContext* context, MachineFunction* func) : context(context), func(func) { }
	void run();

	void allocStackVars();

	void setupArgsWin64();
	void setupArgsUnix64();

	void emitProlog(const std::vector<RegisterName>& savedRegs, const std::vector<RegisterName>& savedXmmRegs, int stackAdjustment, bool dsa);
	void emitEpilog(const std::vector<RegisterName>& savedRegs, const std::vector<RegisterName>& savedXmmRegs, int stackAdjustment, bool dsa);

	bool isFloat(IRType* type) const { return dynamic_cast<IRFloatType*>(type); }
	bool isDouble(IRType* type) const { return dynamic_cast<IRDoubleType*>(type); }

	void createRegisterInfo();

	void useRegister(MachineOperand& operand);
	void killVirtRegister(int vregIndex);

	void assignAllToStack();
	void setAllToStack();

	void assignVirt2Phys(int vregIndex, int pregIndex);
	void assignVirt2StackSlot(int vregIndex);

	void setAsMostRecentlyUsed(int pregIndex);
	void setAsLeastRecentlyUsed(int pregIndex);
	int getLeastRecentlyUsed(MachineRegClass cls);

	void runLiveAnalysis();
	void addLiveReference(size_t vregIndex, MachineBasicBlock* bb);

	void updateModifiedStatus(MachineInst* inst);

	std::string getVRegName(size_t vregIndex);

	std::vector<RegisterName> volatileRegs;
	std::set<RegisterName> usedRegs;

	IRContext* context;
	MachineFunction* func;

	std::vector<RARegisterInfo> reginfo;
	RARegisterClass regclass[2];

	int nextStackOffset = 0;
	std::vector<int> freeStackOffsets;

	std::vector<MachineInst*> emittedInstructions;
};
