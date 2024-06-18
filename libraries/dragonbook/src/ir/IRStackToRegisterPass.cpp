
#include "dragonbook/IRStackToRegisterPass.h"
#include "dragonbook/IRFunction.h"
#include "dragonbook/IRBasicBlock.h"
#include "dragonbook/IRInstVisitor.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>

class IRFindPromotableStackVars : IRInstVisitor
{
public:
	IRFindPromotableStackVars(IRFunction* func) : func(func)
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
	template<typename T> void checkPromote(T*& value)
	{
		auto it = promotableStackVars.find(value);
		if (it != promotableStackVars.end())
		{
			promotableStackVars.erase(it);
		}
	}

	void inst(IRInstLoad* node) override { }
	void inst(IRInstStore* node) override { }
	void inst(IRInstAdd* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstSub* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstFAdd* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstFSub* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstNot* node) override { checkPromote(node->operand); }
	void inst(IRInstNeg* node) override { checkPromote(node->operand); }
	void inst(IRInstFNeg* node) override { checkPromote(node->operand); }
	void inst(IRInstMul* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstFMul* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstSDiv* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstUDiv* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstFDiv* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstSRem* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstURem* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstShl* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstLShr* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstAShr* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstICmpSLT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstICmpULT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstFCmpULT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstICmpSGT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstICmpUGT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstFCmpUGT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstICmpSLE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstICmpULE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstFCmpULE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstICmpSGE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstICmpUGE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstFCmpUGE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstICmpEQ* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstFCmpUEQ* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstICmpNE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstFCmpUNE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstAnd* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstOr* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstXor* node) override { checkPromote(node->operand1); checkPromote(node->operand2); }
	void inst(IRInstTrunc* node) override { checkPromote(node->value); }
	void inst(IRInstZExt* node) override { checkPromote(node->value); }
	void inst(IRInstSExt* node) override { checkPromote(node->value); }
	void inst(IRInstFPTrunc* node) override { checkPromote(node->value); }
	void inst(IRInstFPExt* node) override { checkPromote(node->value); }
	void inst(IRInstFPToUI* node) override { checkPromote(node->value); }
	void inst(IRInstFPToSI* node) override { checkPromote(node->value); }
	void inst(IRInstUIToFP* node) override { checkPromote(node->value); }
	void inst(IRInstSIToFP* node) override { checkPromote(node->value); }
	void inst(IRInstBitCast* node) override { checkPromote(node->value); }
	void inst(IRInstCall* node) override { checkPromote(node->func); for (IRValue*& arg : node->args) checkPromote(arg); }
	void inst(IRInstGEP* node) override { checkPromote(node->ptr); for (IRValue*& index : node->indices) checkPromote(index); }
	void inst(IRInstBr* node) override { }
	void inst(IRInstCondBr* node) override { checkPromote(node->condition); }
	void inst(IRInstRet* node) override { checkPromote(node->operand); }
	void inst(IRInstRetVoid* node) override { }
	void inst(IRInstAlloca* node) override { checkPromote(node->arraySize); }
	void inst(IRInstPhi* node) override { for (auto& value : node->values) checkPromote(value.second); }
};

struct BBVariables
{
	std::unordered_map<IRValue*, IRValue*> firstLoad;
	std::unordered_map<IRValue*, IRValue*> lastStore;
};

class IRPromoteBBVariables : IRInstVisitor
{
public:
	IRPromoteBBVariables(IRFunction* func, const std::unordered_set<IRValue*>& promotableStackVars) : func(func), promotableStackVars(promotableStackVars)
	{
		bbVars.resize(func->basicBlocks.size());
		size_t index = 0;
		for (IRBasicBlock* bb : func->basicBlocks)
		{
			code.clear();
			code.reserve(bb->code.size());
			
			bbVar = &bbVars[index++];
			variableValues.clear();
			loadedValues.clear();

			for (IRInst* inst : bb->code)
			{
				inst->visit(this);
			}
			bb->code.swap(code);
		}
	}

	IRFunction* func = nullptr;
	std::vector<BBVariables> bbVars;

private:
	const std::unordered_set<IRValue*>& promotableStackVars;
	std::vector<IRInst*> code;

	BBVariables* bbVar = nullptr;

	std::unordered_map<IRValue*, IRValue*> variableValues;
	std::unordered_map<IRValue*, IRValue*> loadedValues;

	// Replace any reference to a load value with the latest stored value for the variable
	template<typename T> void checkPromote(T*& value)
	{
		auto it = loadedValues.find(value);
		if (it != loadedValues.end())
			value = static_cast<T*>(it->second);
	}

	void inst(IRInstLoad* node) override
	{
		auto it = promotableStackVars.find(node->operand);
		if (it != promotableStackVars.end())
		{
			auto& varValue = variableValues[node->operand];
			if (!varValue)
			{
				varValue = node;
				bbVar->firstLoad[node->operand] = node;
			}
			loadedValues[node] = varValue;
		}
		else
		{
			code.push_back(node);
		}
	}

	void inst(IRInstStore* node) override
	{
		auto it = promotableStackVars.find(node->operand2);
		if (it != promotableStackVars.end())
		{
			variableValues[node->operand2] = node->operand1;
			bbVar->lastStore[node->operand2] = node->operand1;
		}
		else
		{
			code.push_back(node);
		}
	}

	void inst(IRInstAdd* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstSub* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFAdd* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFSub* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstNot* node) override { checkPromote(node->operand); code.push_back(node); }
	void inst(IRInstNeg* node) override { checkPromote(node->operand); code.push_back(node); }
	void inst(IRInstFNeg* node) override { checkPromote(node->operand); code.push_back(node); }
	void inst(IRInstMul* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFMul* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstSDiv* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstUDiv* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFDiv* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstSRem* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstURem* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstShl* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstLShr* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstAShr* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpSLT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpULT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFCmpULT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpSGT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpUGT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFCmpUGT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpSLE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpULE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFCmpULE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpSGE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpUGE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFCmpUGE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpEQ* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFCmpUEQ* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpNE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFCmpUNE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstAnd* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstOr* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstXor* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstTrunc* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstZExt* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstSExt* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstFPTrunc* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstFPExt* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstFPToUI* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstFPToSI* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstUIToFP* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstSIToFP* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstBitCast* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstCall* node) override { checkPromote(node->func); for (IRValue*& arg : node->args) checkPromote(arg); code.push_back(node); }
	void inst(IRInstGEP* node) override { checkPromote(node->ptr); for (IRValue*& index : node->indices) checkPromote(index); code.push_back(node); }
	void inst(IRInstBr* node) override { code.push_back(node); }
	void inst(IRInstCondBr* node) override { checkPromote(node->condition); code.push_back(node); }
	void inst(IRInstRet* node) override { checkPromote(node->operand); code.push_back(node); }
	void inst(IRInstRetVoid* node) override { code.push_back(node); }
	void inst(IRInstAlloca* node) override { checkPromote(node->arraySize); code.push_back(node); }
	void inst(IRInstPhi* node) override { for (auto& value : node->values) checkPromote(value.second); code.push_back(node); }
};

class IRPromoteBranchVariables : IRInstVisitor
{
public:
	IRPromoteBranchVariables(IRFunction* func, const std::unordered_set<IRValue*>& promotableStackVars, std::vector<BBVariables>& bbVars) : func(func), promotableStackVars(promotableStackVars), bbVars(bbVars)
	{
	}

	IRFunction* func = nullptr;

private:
	const std::unordered_set<IRValue*>& promotableStackVars;
	std::vector<BBVariables>& bbVars;
	std::vector<IRInst*> code;

	template<typename T> void checkPromote(T*& value) { }

	void inst(IRInstLoad* node) override { code.push_back(node); }
	void inst(IRInstStore* node) override { code.push_back(node); }

	void inst(IRInstAdd* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstSub* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFAdd* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFSub* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstNot* node) override { checkPromote(node->operand); code.push_back(node); }
	void inst(IRInstNeg* node) override { checkPromote(node->operand); code.push_back(node); }
	void inst(IRInstFNeg* node) override { checkPromote(node->operand); code.push_back(node); }
	void inst(IRInstMul* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFMul* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstSDiv* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstUDiv* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFDiv* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstSRem* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstURem* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstShl* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstLShr* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstAShr* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpSLT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpULT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFCmpULT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpSGT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpUGT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFCmpUGT* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpSLE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpULE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFCmpULE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpSGE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpUGE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFCmpUGE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpEQ* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFCmpUEQ* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstICmpNE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstFCmpUNE* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstAnd* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstOr* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstXor* node) override { checkPromote(node->operand1); checkPromote(node->operand2); code.push_back(node); }
	void inst(IRInstTrunc* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstZExt* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstSExt* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstFPTrunc* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstFPExt* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstFPToUI* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstFPToSI* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstUIToFP* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstSIToFP* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstBitCast* node) override { checkPromote(node->value); code.push_back(node); }
	void inst(IRInstCall* node) override { checkPromote(node->func); for (IRValue*& arg : node->args) checkPromote(arg); code.push_back(node); }
	void inst(IRInstGEP* node) override { checkPromote(node->ptr); for (IRValue*& index : node->indices) checkPromote(index); code.push_back(node); }
	void inst(IRInstBr* node) override { code.push_back(node); }
	void inst(IRInstCondBr* node) override { checkPromote(node->condition); code.push_back(node); }
	void inst(IRInstRet* node) override { checkPromote(node->operand); code.push_back(node); }
	void inst(IRInstRetVoid* node) override { code.push_back(node); }
	void inst(IRInstAlloca* node) override { checkPromote(node->arraySize); code.push_back(node); }
	void inst(IRInstPhi* node) override { for (auto& value : node->values) checkPromote(value.second); code.push_back(node); }
};

void IRStackToRegisterPass::run(IRFunction* func)
{
	// Step 1. Find all stack variables that can be promoted.
	// We can't promote any stack variable where the pointer to the variable is used directly.
	IRFindPromotableStackVars stackvars(func);

	// Step 2. Reduce basic blocks to only use one load for each variable
	IRPromoteBBVariables promoteBBs(func, stackvars.promotableStackVars);

	// Step 3. Create phi nodes and link variable load values from other basic blocks
	IRPromoteBranchVariables promoteBranches(func, stackvars.promotableStackVars, promoteBBs.bbVars);

	// Step 4. Remove the alloca instructions for the stack variables we fully promoted
	// To do: don't remove the stackvars that may load from uninitialized memory (or introduce IRUndefinedValue)
	std::vector<IRInstAlloca*> remainingStackVars;
	for (IRInstAlloca* var : func->stackVars)
	{
		if (stackvars.promotableStackVars.find(var) == stackvars.promotableStackVars.end())
		{
			remainingStackVars.push_back(var);
		}
	}
	func->stackVars = std::move(remainingStackVars);
}
