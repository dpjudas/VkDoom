
#pragma once

class IRFunction;
class IRInst;
class IRValue;

class IRStackToRegisterPass
{
public:
	static void run(IRFunction* func);
};
