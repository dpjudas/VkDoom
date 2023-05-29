
#include "dragonbook/IRFunction.h"
#include "dragonbook/IRContext.h"
#include "dragonbook/IRInst.h"
#include "dragonbook/IRBasicBlock.h"
#include "dragonbook/IRType.h"
#include <unordered_set>

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

void IRFunction::sortBasicBlocks()
{
	if (basicBlocks.empty())
		return;

	std::unordered_set<IRBasicBlock*> seenBBs;
	std::vector<IRBasicBlock*> sortedBBs;
	std::vector<IRBasicBlock*> bbStack;

	seenBBs.reserve(basicBlocks.size());
	sortedBBs.reserve(basicBlocks.size());
	bbStack.reserve(10);

	bbStack.push_back(basicBlocks.front());
	while (!bbStack.empty())
	{
		IRBasicBlock* bb = bbStack.back();
		bbStack.pop_back();

		if (seenBBs.find(bb) == seenBBs.end())
		{
			sortedBBs.push_back(bb);
			seenBBs.insert(bb);

			if (IRInstCondBr* condbr = dynamic_cast<IRInstCondBr*>(bb->code.back()))
			{
				bbStack.push_back(condbr->bb2);
				bbStack.push_back(condbr->bb1);
			}
			else if (IRInstBr* br = dynamic_cast<IRInstBr*>(bb->code.back()))
			{
				bbStack.push_back(br->bb);
			}
		}
	}

	basicBlocks = std::move(sortedBBs);
}
