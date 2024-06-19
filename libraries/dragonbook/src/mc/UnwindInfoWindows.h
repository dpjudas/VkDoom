#pragma once

#include <vector>
#include <cstdint>

class MachineFunction;

class UnwindInfoWindows
{
public:
	static std::vector<uint16_t> create(MachineFunction* func);
};
