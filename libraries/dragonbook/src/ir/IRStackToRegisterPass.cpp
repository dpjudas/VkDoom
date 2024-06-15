
#include "dragonbook/IRStackToRegisterPass.h"
#include "dragonbook/IRFunction.h"
#include "dragonbook/IRBasicBlock.h"
#include "dragonbook/IRInstVisitor.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>

class IRAnalyzePromote : IRInstVisitor
{
public:
	IRAnalyzePromote(IRFunction* func) : func(func)
	{
		for (IRInstAlloca* var : func->stackVars)
		{
			promotableStackVars.insert(var);
		}

		for (IRBasicBlock* bb : func->basicBlocks)
		{
			for (IRInst* inst : bb->code)
			{
				inst->visit(this);
			}
		}
	}

	IRFunction* func = nullptr;
	std::unordered_set<IRValue*> promotableStackVars;

private:
	// We can't promote any stack variable where the pointer to the variable is used directly
	template<typename T> void checkPromote(T*& value)
	{
		auto it = promotableStackVars.find(value);
		if (it != promotableStackVars.end())
		{
			promotableStackVars.erase(it);
		}
	}

	void inst(IRInstLoad* node) override
	{
	}

	void inst(IRInstStore* node) override
	{
	}

	void inst(IRInstAdd* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstSub* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstFAdd* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstFSub* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstNot* node) override
	{
		checkPromote(node->operand);
	}

	void inst(IRInstNeg* node) override
	{
		checkPromote(node->operand);
	}

	void inst(IRInstFNeg* node) override
	{
		checkPromote(node->operand);
	}

	void inst(IRInstMul* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstFMul* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstSDiv* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstUDiv* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstFDiv* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstSRem* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstURem* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstShl* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstLShr* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstAShr* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstICmpSLT* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstICmpULT* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstFCmpULT* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstICmpSGT* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstICmpUGT* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstFCmpUGT* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstICmpSLE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstICmpULE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstFCmpULE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstICmpSGE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstICmpUGE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstFCmpUGE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstICmpEQ* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstFCmpUEQ* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstICmpNE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstFCmpUNE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstAnd* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstOr* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstXor* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
	}

	void inst(IRInstTrunc* node) override
	{
		checkPromote(node->value);
	}

	void inst(IRInstZExt* node) override
	{
		checkPromote(node->value);
	}

	void inst(IRInstSExt* node) override
	{
		checkPromote(node->value);
	}

	void inst(IRInstFPTrunc* node) override
	{
		checkPromote(node->value);
	}

	void inst(IRInstFPExt* node) override
	{
		checkPromote(node->value);
	}

	void inst(IRInstFPToUI* node) override
	{
		checkPromote(node->value);
	}

	void inst(IRInstFPToSI* node) override
	{
		checkPromote(node->value);
	}

	void inst(IRInstUIToFP* node) override
	{
		checkPromote(node->value);
	}

	void inst(IRInstSIToFP* node) override
	{
		checkPromote(node->value);
	}

	void inst(IRInstBitCast* node) override
	{
		checkPromote(node->value);
	}

	void inst(IRInstCall* node) override
	{
		checkPromote(node->func);
		for (IRValue*& arg : node->args)
			checkPromote(arg);
	}

	void inst(IRInstGEP* node) override
	{
		checkPromote(node->ptr);
		for (IRValue*& index : node->indices)
			checkPromote(index);
	}

	void inst(IRInstBr* node) override
	{
	}

	void inst(IRInstCondBr* node) override
	{
		checkPromote(node->condition);
	}

	void inst(IRInstRet* node) override
	{
		checkPromote(node->operand);
	}

	void inst(IRInstRetVoid* node) override
	{
	}

	void inst(IRInstAlloca* node) override
	{
		checkPromote(node->arraySize);
	}
};

class IRPromoteRegisters : IRInstVisitor
{
public:
	IRPromoteRegisters(const IRAnalyzePromote& analysis) : func(analysis.func)
	{
		for (IRValue* var : analysis.promotableStackVars)
		{
			variables[var] = Variable();
		}

		std::vector<IRInstAlloca*> remainingStackVars;
		for (IRInstAlloca* var : func->stackVars)
		{
			if (analysis.promotableStackVars.find(var) == analysis.promotableStackVars.end())
			{
				remainingStackVars.push_back(var);
			}
		}
		func->stackVars = std::move(remainingStackVars);

		for (IRBasicBlock* bb : func->basicBlocks)
		{
			code.clear();
			code.reserve(bb->code.size());
			for (IRInst* inst : bb->code)
			{
				inst->visit(this);
			}
			bb->code.swap(code);
		}
	}

	IRFunction* func = nullptr;

private:
	std::vector<IRInst*> code;

	struct Variable
	{
		IRValue* storeValue = nullptr; // To do: We need phi nodes for this to work across basic blocks
	};

	std::unordered_map<IRValue*, Variable> variables;
	std::unordered_map<IRValue*, IRValue*> loadValues;

	// Replace any reference to a load value with the latest stored value for the variable
	template<typename T> void checkPromote(T*& value)
	{
		auto it = loadValues.find(value);
		if (it != loadValues.end())
			value = static_cast<T*>(it->second);
	}

	void inst(IRInstLoad* node) override
	{
		auto it = variables.find(node->operand);
		if (it != variables.end())
		{
			loadValues[node] = it->second.storeValue;
		}
		else
		{
			code.push_back(node);
		}
	}

	void inst(IRInstStore* node) override
	{
		auto it = variables.find(node->operand2);
		if (it != variables.end())
		{
			it->second.storeValue = node->operand1;
		}
		else
		{
			code.push_back(node);
		}
	}

	void inst(IRInstAdd* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstSub* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstFAdd* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstFSub* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstNot* node) override
	{
		checkPromote(node->operand);
		code.push_back(node);
	}

	void inst(IRInstNeg* node) override
	{
		checkPromote(node->operand);
		code.push_back(node);
	}

	void inst(IRInstFNeg* node) override
	{
		checkPromote(node->operand);
		code.push_back(node);
	}

	void inst(IRInstMul* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstFMul* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstSDiv* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstUDiv* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstFDiv* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstSRem* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstURem* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstShl* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstLShr* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstAShr* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstICmpSLT* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstICmpULT* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstFCmpULT* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstICmpSGT* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstICmpUGT* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstFCmpUGT* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstICmpSLE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstICmpULE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstFCmpULE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstICmpSGE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstICmpUGE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstFCmpUGE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstICmpEQ* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstFCmpUEQ* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstICmpNE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstFCmpUNE* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstAnd* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstOr* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstXor* node) override
	{
		checkPromote(node->operand1);
		checkPromote(node->operand2);
		code.push_back(node);
	}

	void inst(IRInstTrunc* node) override
	{
		checkPromote(node->value);
		code.push_back(node);
	}

	void inst(IRInstZExt* node) override
	{
		checkPromote(node->value);
		code.push_back(node);
	}

	void inst(IRInstSExt* node) override
	{
		checkPromote(node->value);
		code.push_back(node);
	}

	void inst(IRInstFPTrunc* node) override
	{
		checkPromote(node->value);
		code.push_back(node);
	}

	void inst(IRInstFPExt* node) override
	{
		checkPromote(node->value);
		code.push_back(node);
	}

	void inst(IRInstFPToUI* node) override
	{
		checkPromote(node->value);
		code.push_back(node);
	}

	void inst(IRInstFPToSI* node) override
	{
		checkPromote(node->value);
		code.push_back(node);
	}

	void inst(IRInstUIToFP* node) override
	{
		checkPromote(node->value);
		code.push_back(node);
	}

	void inst(IRInstSIToFP* node) override
	{
		checkPromote(node->value);
		code.push_back(node);
	}

	void inst(IRInstBitCast* node) override
	{
		checkPromote(node->value);
		code.push_back(node);
	}

	void inst(IRInstCall* node) override
	{
		checkPromote(node->func);
		for (IRValue*& arg : node->args)
			checkPromote(arg);
		code.push_back(node);
	}

	void inst(IRInstGEP* node) override
	{
		checkPromote(node->ptr);
		for (IRValue*& index : node->indices)
			checkPromote(index);
		code.push_back(node);
	}

	void inst(IRInstBr* node) override
	{
		code.push_back(node);
	}

	void inst(IRInstCondBr* node) override
	{
		checkPromote(node->condition);
		code.push_back(node);
	}

	void inst(IRInstRet* node) override
	{
		checkPromote(node->operand);
		code.push_back(node);
	}

	void inst(IRInstRetVoid* node) override
	{
		code.push_back(node);
	}

	void inst(IRInstAlloca* node) override
	{
		checkPromote(node->arraySize);
		code.push_back(node);
	}
};

void IRStackToRegisterPass::run(IRFunction* func)
{
	IRAnalyzePromote analysis(func);
	IRPromoteRegisters promote(analysis);
}
