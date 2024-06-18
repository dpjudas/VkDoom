#pragma once

#include "IRValue.h"

class IRType;
class IRFunctionType;
class IRBasicBlock;
class IRInstAlloca;

class IRFunction : public IRConstant
{
public:
	IRFunction(IRContext *context, IRFunctionType *type, const std::string &name);

	IRInstAlloca *createAlloca(IRType *type, IRValue *arraySize, const std::string &name = {});
	IRBasicBlock *createBasicBlock(const std::string &comment);

	void sortBasicBlocks();

	IRContext *context;
	std::string name;
	std::vector<IRValue *> args;
	std::vector<IRBasicBlock *> basicBlocks;
	std::vector<IRInstAlloca *> stackVars;
	std::vector<std::string> fileInfo;
};
