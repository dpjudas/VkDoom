#pragma once

#include "IRInstVisitor.h"
#include "IRInst.h"

class IRContext;
class IRFunction;
class IRInst;

class IRBasicBlock
{
public:
	IRBasicBlock(IRFunction *func, std::string comment) : function(func), comment(comment) { }

	IRFunction *function;
	std::string comment;
	std::vector<IRInst *> code;

	void visit(IRInstVisitor *visitor) { for (IRInst *node : code) node->visit(visitor); }
};
