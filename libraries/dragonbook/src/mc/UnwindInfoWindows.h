#pragma once

#include <vector>

class MachineFunction;

class UnwindInfoWindows
{
public:
	static std::vector<uint16_t> create(MachineFunction* func);
};
