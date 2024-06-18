
#pragma once

#include "IRInst.h"
#include "IRBasicBlock.h"
#include "IRFunction.h"
#include "IRInstValidator.h"

class IRValue;
class IRType;

class IRBuilder
{
public:
	void SetInsertPoint(IRBasicBlock *bb) { this->bb = bb; }
	IRBasicBlock *GetInsertBlock() { return bb; }
	void ClearInsertionPoint() { bb = nullptr; }

	IRInstLoad *CreateLoad(IRValue *);
	IRInstStore *CreateStore(IRValue *, IRValue *);

	IRInstAlloca* CreateAlloca(IRType* type, IRValue* arraySize, const std::string& name = {});
	IRInstPhi* CreatePhi(IRType* type, const std::vector<std::pair<IRBasicBlock*, IRValue*>>& values);

	IRInstCall *CreateCall(IRValue *func, const std::vector<IRValue *> &args = {});

	IRInstGEP *CreateGEP(IRValue *, const std::vector<IRValue *> &);
	IRInstGEP *CreateConstGEP1_32(IRValue *, int);
	IRInstGEP *CreateConstGEP2_32(IRValue *, int, int);

	IRInstTrunc *CreateTrunc(IRValue *, IRType *);
	IRInstZExt *CreateZExt(IRValue *, IRType *);
	IRInstSExt *CreateSExt(IRValue *, IRType *);
	IRInstFPTrunc *CreateFPTrunc(IRValue *, IRType *);
	IRInstFPExt *CreateFPExt(IRValue *, IRType *);
	IRInstFPToUI *CreateFPToUI(IRValue *, IRType *);
	IRInstFPToSI *CreateFPToSI(IRValue *, IRType *);
	IRInstUIToFP *CreateUIToFP(IRValue *, IRType *);
	IRInstSIToFP *CreateSIToFP(IRValue *, IRType *);
	IRInstBitCast *CreateBitCast(IRValue *, IRType *);

	IRInstAdd *CreateAdd(IRValue *, IRValue *);
	IRInstSub *CreateSub(IRValue *, IRValue *);
	IRInstFAdd *CreateFAdd(IRValue *, IRValue *);
	IRInstFSub *CreateFSub(IRValue *, IRValue *);

	IRInstNot *CreateNot(IRValue *);
	IRInstNeg *CreateNeg(IRValue *);
	IRInstFNeg *CreateFNeg(IRValue *);

	IRInstMul *CreateMul(IRValue *, IRValue *);
	IRInstFMul *CreateFMul(IRValue *, IRValue *);

	IRInstSDiv *CreateSDiv(IRValue *, IRValue *);
	IRInstUDiv *CreateUDiv(IRValue *, IRValue *);
	IRInstFDiv *CreateFDiv(IRValue *, IRValue *);

	IRInstSRem *CreateSRem(IRValue *, IRValue *);
	IRInstURem *CreateURem(IRValue *, IRValue *);

	IRInstShl *CreateShl(IRValue *, IRValue *);
	IRInstLShr *CreateLShr(IRValue *, IRValue *);
	IRInstAShr *CreateAShr(IRValue *, IRValue *);

	IRInstICmpSLT *CreateICmpSLT(IRValue *, IRValue *);
	IRInstICmpULT *CreateICmpULT(IRValue *, IRValue *);
	IRInstFCmpULT *CreateFCmpULT(IRValue *, IRValue *);

	IRInstICmpSGT *CreateICmpSGT(IRValue *, IRValue *);
	IRInstICmpUGT *CreateICmpUGT(IRValue *, IRValue *);
	IRInstFCmpUGT *CreateFCmpUGT(IRValue *, IRValue *);

	IRInstICmpSLE *CreateICmpSLE(IRValue *, IRValue *);
	IRInstICmpULE *CreateICmpULE(IRValue *, IRValue *);
	IRInstFCmpULE *CreateFCmpULE(IRValue *, IRValue *);

	IRInstICmpSGE *CreateICmpSGE(IRValue *, IRValue *);
	IRInstICmpUGE *CreateICmpUGE(IRValue *, IRValue *);
	IRInstFCmpUGE *CreateFCmpUGE(IRValue *, IRValue *);

	IRInstICmpEQ *CreateICmpEQ(IRValue *, IRValue *);
	IRInstFCmpUEQ *CreateFCmpUEQ(IRValue *, IRValue *);

	IRInstICmpNE *CreateICmpNE(IRValue *, IRValue *);
	IRInstFCmpUNE *CreateFCmpUNE(IRValue *, IRValue *);

	IRInstAnd *CreateAnd(IRValue *, IRValue *);
	IRInstOr *CreateOr(IRValue *, IRValue *);
	IRInstXor *CreateXor(IRValue *, IRValue *);

	IRInstBr *CreateBr(IRBasicBlock *);
	IRInstCondBr *CreateCondBr(IRValue *, IRBasicBlock *, IRBasicBlock *);
	IRInstRet *CreateRet(IRValue *);
	IRInstRetVoid *CreateRetVoid();

private:
	template<typename T, typename... Args>
	T *add(Args && ... args)
	{
		T *inst = bb->function->context->newValue<T>(std::forward<Args>(args)...);
		IRInstValidator validator;
		inst->visit(&validator);
		bb->code.push_back(inst);
		return inst;
	}

	template<typename T, typename... Args>
	T* addCmp(Args&& ... args)
	{
		return add<T>(bb->function->context->getInt1Ty(), std::forward<Args>(args)...);
	}

	IRBasicBlock *bb = nullptr;
};