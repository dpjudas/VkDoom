
#include "dragonbook/IRFunction.h"
#include "dragonbook/IRContext.h"
#include "dragonbook/IRInst.h"
#include "dragonbook/IRBasicBlock.h"
#include "dragonbook/IRType.h"

IRFunction::IRFunction(IRContext *context, IRFunctionType *type, const std::string &name) : IRConstant(type), context(context), name(name)
{
	int index = 0;
	args.reserve(type->args.size());
	for (IRType *argType : type->args)
	{
		args.push_back(context->newValue<IRFunctionArg>(argType, index++));
	}
}

IRInstAlloca *IRFunction::createAlloca(IRType *type, IRValue *arraySize, const std::string &name)
{
	if (!arraySize)
		arraySize = context->getConstantInt(1);
	auto inst = context->newValue<IRInstAlloca>(context, type, arraySize, name);
	stackVars.push_back(inst);
	return inst;
}

IRBasicBlock *IRFunction::createBasicBlock(const std::string &comment)
{
	auto bb = context->newBasicBlock(this, comment);
	basicBlocks.push_back(bb);
	return bb;
}
