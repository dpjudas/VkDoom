#pragma once

#include <cstdint>
#include <vector>
#include "dragonbook/IR.h"

class MachineBasicBlock;

enum class MachineInstOpcode
{
	nop,
	lea,
	loadss, loadsd, load64, load32, load16, load8, // mov reg,ptr
	storess, storesd, store64, store32, store16, store8, // mov ptr,reg
	movss, movsd, mov64, mov32, mov16, mov8,
	movsx8_16, movsx8_32, movsx8_64, movsx16_32, movsx16_64, movsx32_64,
	movzx8_16, movzx8_32, movzx8_64, movzx16_32, movzx16_64,
	addss, addsd, add64, add32, add16, add8,
	subss, subsd, sub64, sub32, sub16, sub8,
	not64, not32, not16, not8,
	neg64, neg32, neg16, neg8,
	shl64, shl32, shl16, shl8,
	shr64, shr32, shr16, shr8,
	sar64, sar32, sar16, sar8,
	and64, and32, and16, and8,
	or64, or32, or16, or8,
	xorpd, xorps, xor64, xor32, xor16, xor8,
	mulss, mulsd, imul64, imul32, imul16, imul8,
	divss, divsd, idiv64, idiv32, idiv16, idiv8,
	div64, div32, div16, div8,
	ucomisd, ucomiss, cmp64, cmp32, cmp16, cmp8,
	setl, setb, seta, setg, setle, setbe, setae, setge, sete, setne,
	cvtsd2ss, cvtss2sd,
	cvttsd2si, cvttss2si,
	cvtsi2sd, cvtsi2ss,
	jmp, je, jne,
	call, ret, push, pop, movdqa
};

enum class MachineOperandType
{
	reg, constant, frameOffset, spillOffset, stackOffset, imm, basicblock, func, global
};

class MachineOperand
{
public:
	MachineOperandType type;
	union
	{
		int registerIndex;
		int constantIndex;
		int frameOffset;
		int spillOffset;
		int stackOffset;
		uint64_t immvalue;
		MachineBasicBlock* bb;
		IRFunction* func;
		IRGlobalVariable* global;
	};
};

enum class MachineUnwindHint
{
	None,
	PushNonvolatile,
	StackAdjustment,
	RegisterStackLocation,
	SaveFrameRegister
};

class MachineInst
{
public:
	MachineInstOpcode opcode = MachineInstOpcode::nop;
	std::vector<MachineOperand> operands;

	int unwindOffset = -1;
	MachineUnwindHint unwindHint = MachineUnwindHint::None;

	std::string comment;
	int fileIndex = -1;
	int lineNumber = -1;
};

class MachineBasicBlock
{
public:
	std::vector<MachineInst*> code;
};

class MachineConstant
{
public:
	MachineConstant(const void* value, int size) : size(size)
	{
		if (size > maxsize)
			throw std::runtime_error("Constant data type too large");
		memcpy(data, value, size);
		memset(data + size, 0, maxsize - size);
	}

	enum { maxsize = 16 };
	uint8_t data[maxsize];
	int size;
};

enum class MachineRegClass
{
	gp,
	xmm,
	reserved
};

class MachineRegister
{
public:
	MachineRegister() = default;
	MachineRegister(MachineRegClass cls) : cls(cls) { }

	MachineRegClass cls = MachineRegClass::reserved;
};

class MachineStackAlloc
{
public:
	MachineStackAlloc() = default;
	MachineStackAlloc(int registerIndex, size_t size, std::string name) : registerIndex(registerIndex), size(size), name(std::move(name)) { }

	int registerIndex = 0;
	size_t size = 0;
	std::string name;
};

class MachineFunction
{
public:
	MachineFunction() = default;
	MachineFunction(std::string name) : name(std::move(name)) { }

	std::string name;
	IRFunctionType* type = nullptr;
	MachineBasicBlock* prolog = nullptr;
	MachineBasicBlock* epilog = nullptr;
	std::vector<MachineBasicBlock*> basicBlocks;
	std::vector<MachineConstant> constants;
	std::vector<MachineRegister> registers;
	std::vector<MachineStackAlloc> stackvars;
	int maxCallArgsSize = 0;
	int frameBaseOffset = 0;
	int spillBaseOffset = 0;
	bool dynamicStackAllocations = false;
};

enum class RegisterName : int
{
	rax,
	rcx,
	rdx,
	rbx,
	rsp,
	rbp,
	rsi,
	rdi,
	r8,
	r9,
	r10,
	r11,
	r12,
	r13,
	r14,
	r15,
	xmm0,
	xmm1,
	xmm2,
	xmm3,
	xmm4,
	xmm5,
	xmm6,
	xmm7,
	xmm8,
	xmm9,
	xmm10,
	xmm11,
	xmm12,
	xmm13,
	xmm14,
	xmm15,
	vregstart = 128
};
