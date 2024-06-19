#pragma once

#include <vector>
#include <cstdint>

class MachineFunction;

class UnwindInfoUnix
{
public:
	static std::vector<uint8_t> create(MachineFunction* func, unsigned int& functionStart);

private:
	static void writeLength(std::vector<uint8_t>& stream, unsigned int pos, unsigned int v);
	static void writeUInt64(std::vector<uint8_t>& stream, uint64_t v);
	static void writeUInt32(std::vector<uint8_t>& stream, uint32_t v);
	static void writeUInt16(std::vector<uint8_t>& stream, uint16_t v);
	static void writeUInt8(std::vector<uint8_t>& stream, uint8_t v);
	static void writeULEB128(std::vector<uint8_t>& stream, uint32_t v);
	static void writeSLEB128(std::vector<uint8_t>& stream, int32_t v);
	static void writePadding(std::vector<uint8_t>& stream);
	static void writeCIE(std::vector<uint8_t>& stream, const std::vector<uint8_t>& cieInstructions, uint8_t returnAddressReg);
	static void writeFDE(std::vector<uint8_t>& stream, const std::vector<uint8_t>& fdeInstructions, uint32_t cieLocation, unsigned int& functionStart);
	static void writeAdvanceLoc(std::vector<uint8_t>& fdeInstructions, uint64_t offset, uint64_t& lastOffset);
	static void writeDefineCFA(std::vector<uint8_t>& cieInstructions, int dwarfRegId, int stackOffset);
	static void writeDefineStackOffset(std::vector<uint8_t>& fdeInstructions, int stackOffset);
	static void writeRegisterStackLocation(std::vector<uint8_t>& instructions, int dwarfRegId, int stackLocation);
};
