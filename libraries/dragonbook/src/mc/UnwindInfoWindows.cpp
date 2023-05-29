
#include "UnwindInfoWindows.h"
#include "MachineInst.h"

#define UWOP_PUSH_NONVOL 0
#define UWOP_ALLOC_LARGE 1
#define UWOP_ALLOC_SMALL 2
#define UWOP_SET_FPREG 3
#define UWOP_SAVE_NONVOL 4
#define UWOP_SAVE_NONVOL_FAR 5
#define UWOP_SAVE_XMM128 8
#define UWOP_SAVE_XMM128_FAR 9
#define UWOP_PUSH_MACHFRAME 10

std::vector<uint16_t> UnwindInfoWindows::create(MachineFunction* func)
{
	uint16_t version = 1, flags = 0, frameRegister = 0, frameOffset = 0;

	// Build UNWIND_CODE codes:

	std::vector<uint16_t> codes;
	uint32_t opoffset, opcode, opinfo;

	int lastOffset = 0;
	for (auto inst : func->prolog->code)
	{
		if (inst->unwindHint == MachineUnwindHint::PushNonvolatile)
		{
			opoffset = (uint32_t)inst->unwindOffset;
			opcode = UWOP_PUSH_NONVOL;
			opinfo = inst->operands[0].registerIndex;
			codes.push_back(opoffset | (opcode << 8) | (opinfo << 12));
		}
		else if (inst->unwindHint == MachineUnwindHint::StackAdjustment)
		{
			uint32_t stackadjust = (uint32_t)inst->operands[1].immvalue;
			if (stackadjust <= 128)
			{
				opoffset = (uint32_t)inst->unwindOffset;
				opcode = UWOP_ALLOC_SMALL;
				opinfo = stackadjust / 8 - 1;
				codes.push_back(opoffset | (opcode << 8) | (opinfo << 12));
			}
			else if (stackadjust <= 512 * 1024 - 8)
			{
				opoffset = (uint32_t)inst->unwindOffset;
				opcode = UWOP_ALLOC_LARGE;
				opinfo = 0;
				codes.push_back(stackadjust / 8);
				codes.push_back(opoffset | (opcode << 8) | (opinfo << 12));
			}
			else
			{
				opoffset = (uint32_t)inst->unwindOffset;
				opcode = UWOP_ALLOC_LARGE;
				opinfo = 1;
				codes.push_back((uint16_t)(stackadjust >> 16));
				codes.push_back((uint16_t)stackadjust);
				codes.push_back(opoffset | (opcode << 8) | (opinfo << 12));
			}
		}
		else if (inst->unwindHint == MachineUnwindHint::RegisterStackLocation)
		{
			uint32_t vecSize = 16;
			int stackoffset = inst->operands[0].stackOffset;
			if (stackoffset / vecSize < (1 << 16))
			{
				opoffset = (uint32_t)inst->unwindOffset;
				opcode = UWOP_SAVE_XMM128;
				opinfo = inst->operands[1].registerIndex;
				codes.push_back(stackoffset / vecSize);
				codes.push_back(opoffset | (opcode << 8) | (opinfo << 12));
			}
			else
			{
				opoffset = (uint32_t)inst->unwindOffset;
				opcode = UWOP_SAVE_XMM128_FAR;
				opinfo = inst->operands[1].registerIndex;
				codes.push_back((uint16_t)(stackoffset >> 16));
				codes.push_back((uint16_t)stackoffset);
				codes.push_back(opoffset | (opcode << 8) | (opinfo << 12));
			}
		}
		else if (inst->unwindHint == MachineUnwindHint::SaveFrameRegister)
		{
			opoffset = (uint32_t)inst->unwindOffset;
			opcode = UWOP_SET_FPREG;
			opinfo = 0;
			frameRegister = inst->operands[0].registerIndex;
			codes.push_back(opoffset | (opcode << 8) | (opinfo << 12));
		}

		if (inst->opcode != MachineInstOpcode::jmp)
			lastOffset = inst->unwindOffset;
	}

	// Build the UNWIND_INFO structure:

	uint16_t sizeOfProlog = (uint16_t)lastOffset;
	uint16_t countOfCodes = (uint16_t)codes.size();

	std::vector<uint16_t> info;
	info.push_back(version | (flags << 3) | (sizeOfProlog << 8));
	info.push_back(countOfCodes | (frameRegister << 8) | (frameOffset << 12));

	for (size_t i = codes.size(); i > 0; i--)
		info.push_back(codes[i - 1]);

	if (codes.size() % 2 == 1)
		info.push_back(0);

	return info;
}
