
#include "UnwindInfoUnix.h"
#include "MachineInst.h"

std::vector<uint8_t> UnwindInfoUnix::create(MachineFunction* func, unsigned int& functionStart)
{
	// Build .eh_frame:
	//
	// The documentation for this can be found in the DWARF standard
	// The x64 specific details are described in "System V Application Binary Interface AMD64 Architecture Processor Supplement"

	int dwarfRegId[16];
	dwarfRegId[(int)RegisterName::rax] = 0;
	dwarfRegId[(int)RegisterName::rdx] = 1;
	dwarfRegId[(int)RegisterName::rcx] = 2;
	dwarfRegId[(int)RegisterName::rbx] = 3;
	dwarfRegId[(int)RegisterName::rsi] = 4;
	dwarfRegId[(int)RegisterName::rdi] = 5;
	dwarfRegId[(int)RegisterName::rbp] = 6;
	dwarfRegId[(int)RegisterName::rsp] = 7;
	dwarfRegId[(int)RegisterName::r8] = 8;
	dwarfRegId[(int)RegisterName::r9] = 9;
	dwarfRegId[(int)RegisterName::r10] = 10;
	dwarfRegId[(int)RegisterName::r11] = 11;
	dwarfRegId[(int)RegisterName::r12] = 12;
	dwarfRegId[(int)RegisterName::r13] = 13;
	dwarfRegId[(int)RegisterName::r14] = 14;
	dwarfRegId[(int)RegisterName::r15] = 15;
	int dwarfRegRAId = 16;
	int dwarfRegXmmId = 17;

	std::vector<uint8_t> cieInstructions;
	std::vector<uint8_t> fdeInstructions;

	uint8_t returnAddressReg = dwarfRegRAId;
	int stackOffset = 8; // Offset from RSP to the Canonical Frame Address (CFA) - stack position where the CALL return address is stored

	writeDefineCFA(cieInstructions, dwarfRegId[(int)RegisterName::rsp], stackOffset);
	writeRegisterStackLocation(cieInstructions, returnAddressReg, stackOffset);

	uint64_t lastOffset = 0;
	for (auto inst : func->prolog->code)
	{
		if (inst->unwindHint == MachineUnwindHint::PushNonvolatile)
		{
			stackOffset += 8;
			writeAdvanceLoc(fdeInstructions, inst->unwindOffset, lastOffset);
			writeDefineStackOffset(fdeInstructions, stackOffset);
			writeRegisterStackLocation(fdeInstructions, dwarfRegId[inst->operands[0].registerIndex], stackOffset);
		}
		else if (inst->unwindHint == MachineUnwindHint::StackAdjustment)
		{
			uint32_t stackadjust = (uint32_t)inst->operands[1].immvalue;
			stackOffset += stackadjust;
			writeAdvanceLoc(fdeInstructions, inst->unwindOffset, lastOffset);
			writeDefineStackOffset(fdeInstructions, stackOffset);
		}
		else if (inst->unwindHint == MachineUnwindHint::RegisterStackLocation)
		{
			uint32_t vecSize = 16;
			int vecOffset = inst->operands[0].stackOffset;
			writeAdvanceLoc(fdeInstructions, inst->unwindOffset, lastOffset);
			writeRegisterStackLocation(fdeInstructions, dwarfRegXmmId + inst->operands[1].registerIndex, stackOffset - vecOffset);
		}
	}

	std::vector<uint8_t> stream;
	writeCIE(stream, cieInstructions, returnAddressReg);
	writeFDE(stream, fdeInstructions, 0, functionStart);
	writeUInt32(stream, 0);
	return stream;
}

void UnwindInfoUnix::writeLength(std::vector<uint8_t>& stream, unsigned int pos, unsigned int v)
{
	*(uint32_t*)(&stream[pos]) = v;
}

void UnwindInfoUnix::writeUInt64(std::vector<uint8_t>& stream, uint64_t v)
{
	for (int i = 0; i < 8; i++)
		stream.push_back((v >> (i * 8)) & 0xff);
}

void UnwindInfoUnix::writeUInt32(std::vector<uint8_t>& stream, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		stream.push_back((v >> (i * 8)) & 0xff);
}

void UnwindInfoUnix::writeUInt16(std::vector<uint8_t>& stream, uint16_t v)
{
	for (int i = 0; i < 2; i++)
		stream.push_back((v >> (i * 8)) & 0xff);
}

void UnwindInfoUnix::writeUInt8(std::vector<uint8_t>& stream, uint8_t v)
{
	stream.push_back(v);
}

void UnwindInfoUnix::writeULEB128(std::vector<uint8_t>& stream, uint32_t v)
{
	while (true)
	{
		if (v < 128)
		{
			writeUInt8(stream, v);
			break;
		}
		else
		{
			writeUInt8(stream, (v & 0x7f) | 0x80);
			v >>= 7;
		}
	}
}

void UnwindInfoUnix::writeSLEB128(std::vector<uint8_t>& stream, int32_t v)
{
	if (v >= 0)
	{
		writeULEB128(stream, v);
	}
	else
	{
		while (true)
		{
			if (v > -128)
			{
				writeUInt8(stream, v & 0x7f);
				break;
			}
			else
			{
				writeUInt8(stream, v);
				v >>= 7;
			}
		}
	}
}

void UnwindInfoUnix::writePadding(std::vector<uint8_t>& stream)
{
	int padding = stream.size() % 8;
	if (padding != 0)
	{
		padding = 8 - padding;
		for (int i = 0; i < padding; i++) writeUInt8(stream, 0);
	}
}

void UnwindInfoUnix::writeCIE(std::vector<uint8_t>& stream, const std::vector<uint8_t>& cieInstructions, uint8_t returnAddressReg)
{
	unsigned int lengthPos = (unsigned int)stream.size();
	writeUInt32(stream, 0); // Length
	writeUInt32(stream, 0); // CIE ID

	writeUInt8(stream, 1); // CIE Version
	writeUInt8(stream, 'z');
	writeUInt8(stream, 'R'); // fde encoding
	writeUInt8(stream, 0);
	writeULEB128(stream, 1);
	writeSLEB128(stream, -1);
	writeULEB128(stream, returnAddressReg);

	writeULEB128(stream, 1); // LEB128 augmentation size
	writeUInt8(stream, 0); // DW_EH_PE_absptr (FDE uses absolute pointers)

	for (unsigned int i = 0; i < cieInstructions.size(); i++)
		stream.push_back(cieInstructions[i]);

	writePadding(stream);
	writeLength(stream, lengthPos, (unsigned int)stream.size() - lengthPos - 4);
}

void UnwindInfoUnix::writeFDE(std::vector<uint8_t>& stream, const std::vector<uint8_t>& fdeInstructions, uint32_t cieLocation, unsigned int& functionStart)
{
	unsigned int lengthPos = (unsigned int)stream.size();
	writeUInt32(stream, 0); // Length
	uint32_t offsetToCIE = (unsigned int)stream.size() - cieLocation;
	writeUInt32(stream, offsetToCIE);

	functionStart = (unsigned int)stream.size();
	writeUInt64(stream, 0); // func start
	writeUInt64(stream, 0); // func size

	writeULEB128(stream, 0); // LEB128 augmentation size

	for (size_t i = 0; i < fdeInstructions.size(); i++)
		stream.push_back(fdeInstructions[i]);

	writePadding(stream);
	writeLength(stream, lengthPos, (unsigned int)stream.size() - lengthPos - 4);
}

void UnwindInfoUnix::writeAdvanceLoc(std::vector<uint8_t>& fdeInstructions, uint64_t offset, uint64_t& lastOffset)
{
	uint64_t delta = offset - lastOffset;
	if (delta < (1 << 6))
	{
		writeUInt8(fdeInstructions, (1 << 6) | (uint8_t)delta); // DW_CFA_advance_loc
	}
	else if (delta < (1 << 8))
	{
		writeUInt8(fdeInstructions, 2); // DW_CFA_advance_loc1
		writeUInt8(fdeInstructions, (uint8_t)delta);
	}
	else if (delta < (1 << 16))
	{
		writeUInt8(fdeInstructions, 3); // DW_CFA_advance_loc2
		writeUInt16(fdeInstructions, (uint16_t)delta);
	}
	else
	{
		writeUInt8(fdeInstructions, 4); // DW_CFA_advance_loc3
		writeUInt32(fdeInstructions, (uint32_t)delta);
	}
	lastOffset = offset;
}

void UnwindInfoUnix::writeDefineCFA(std::vector<uint8_t>& cieInstructions, int dwarfRegId, int stackOffset)
{
	writeUInt8(cieInstructions, 0x0c); // DW_CFA_def_cfa
	writeULEB128(cieInstructions, dwarfRegId);
	writeULEB128(cieInstructions, stackOffset);
}

void UnwindInfoUnix::writeDefineStackOffset(std::vector<uint8_t>& fdeInstructions, int stackOffset)
{
	writeUInt8(fdeInstructions, 0x0e); // DW_CFA_def_cfa_offset
	writeULEB128(fdeInstructions, stackOffset);
}

void UnwindInfoUnix::writeRegisterStackLocation(std::vector<uint8_t>& instructions, int dwarfRegId, int stackLocation)
{
	writeUInt8(instructions, (2 << 6) | dwarfRegId); // DW_CFA_offset
	writeULEB128(instructions, stackLocation);
}
