
#include "dragonbook/IRBuilder.h"
#include "dragonbook/IRBasicBlock.h"
#include "dragonbook/IRType.h"
#include "dragonbook/IRFunction.h"
#include "dragonbook/IRContext.h"

IRInstLoad *IRBuilder::CreateLoad(IRValue *a)
{
	return add<IRInstLoad>(a);
}

IRInstStore *IRBuilder::CreateStore(IRValue *a, IRValue *b)
{
	return add<IRInstStore>(a, b);
}

IRInstAlloca* IRBuilder::CreateAlloca(IRType* type, IRValue* arraySize, const std::string& name)
{
	return add<IRInstAlloca>(bb->function->context, type, arraySize, name);
}

IRInstCall *IRBuilder::CreateCall(IRValue *func, const std::vector<IRValue *> &args)
{
	return add<IRInstCall>(func, args);
}

IRInstGEP *IRBuilder::CreateGEP(IRValue *a, const std::vector<IRValue *> &b)
{
	return add<IRInstGEP>(bb->function->context, a, b);
}

IRInstGEP *IRBuilder::CreateConstGEP1_32(IRValue *a, int b)
{
	IRContext *context = bb->function->context;
	return CreateGEP(a, { context->getConstantInt(context->getInt32Ty(), b) });
}

IRInstGEP *IRBuilder::CreateConstGEP2_32(IRValue *a, int b, int c)
{
	IRContext *context = bb->function->context;
	return CreateGEP(a, { context->getConstantInt(context->getInt32Ty(), b), context->getConstantInt(context->getInt32Ty(), c) });
}

IRInstTrunc *IRBuilder::CreateTrunc(IRValue *a, IRType *b)
{
	return add<IRInstTrunc>(a, b);
}

IRInstZExt *IRBuilder::CreateZExt(IRValue *a, IRType *b)
{
	return add<IRInstZExt>(a, b);
}

IRInstSExt *IRBuilder::CreateSExt(IRValue *a, IRType *b)
{
	return add<IRInstSExt>(a, b);
}

IRInstFPTrunc *IRBuilder::CreateFPTrunc(IRValue *a, IRType *b)
{
	return add<IRInstFPTrunc>(a, b);
}

IRInstFPExt *IRBuilder::CreateFPExt(IRValue *a, IRType *b)
{
	return add<IRInstFPExt>(a, b);
}

IRInstFPToUI *IRBuilder::CreateFPToUI(IRValue *a, IRType *b)
{
	return add<IRInstFPToUI>(a, b);
}

IRInstFPToSI *IRBuilder::CreateFPToSI(IRValue *a, IRType *b)
{
	return add<IRInstFPToSI>(a, b);
}

IRInstUIToFP *IRBuilder::CreateUIToFP(IRValue *a, IRType *b)
{
	return add<IRInstUIToFP>(a, b);
}

IRInstSIToFP *IRBuilder::CreateSIToFP(IRValue *a, IRType *b)
{
	return add<IRInstSIToFP>(a, b);
}

IRInstBitCast *IRBuilder::CreateBitCast(IRValue *a, IRType *b)
{
	return add<IRInstBitCast>(a, b);
}

IRInstAdd *IRBuilder::CreateAdd(IRValue *a, IRValue *b)
{
	return add<IRInstAdd>(a, b);
}

IRInstSub *IRBuilder::CreateSub(IRValue *a, IRValue *b)
{
	return add<IRInstSub>(a, b);
}

IRInstFAdd *IRBuilder::CreateFAdd(IRValue *a, IRValue *b)
{
	return add<IRInstFAdd>(a, b);
}

IRInstFSub *IRBuilder::CreateFSub(IRValue *a, IRValue *b)
{
	return add<IRInstFSub>(a, b);
}

IRInstNot *IRBuilder::CreateNot(IRValue *a)
{
	return add<IRInstNot>(a);
}

IRInstNeg *IRBuilder::CreateNeg(IRValue *a)
{
	return add<IRInstNeg>(a);
}

IRInstFNeg *IRBuilder::CreateFNeg(IRValue *a)
{
	return add<IRInstFNeg>(a);
}

IRInstMul *IRBuilder::CreateMul(IRValue *a, IRValue *b)
{
	return add<IRInstMul>(a, b);
}

IRInstFMul *IRBuilder::CreateFMul(IRValue *a, IRValue *b)
{
	return add<IRInstFMul>(a, b);
}

IRInstSDiv *IRBuilder::CreateSDiv(IRValue *a, IRValue *b)
{
	return add<IRInstSDiv>(a, b);
}

IRInstUDiv *IRBuilder::CreateUDiv(IRValue *a, IRValue *b)
{
	return add<IRInstUDiv>(a, b);
}

IRInstFDiv *IRBuilder::CreateFDiv(IRValue *a, IRValue *b)
{
	return add<IRInstFDiv>(a, b);
}

IRInstSRem *IRBuilder::CreateSRem(IRValue *a, IRValue *b)
{
	return add<IRInstSRem>(a, b);
}

IRInstURem *IRBuilder::CreateURem(IRValue *a, IRValue *b)
{
	return add<IRInstURem>(a, b);
}

IRInstShl *IRBuilder::CreateShl(IRValue *a, IRValue *b)
{
	return add<IRInstShl>(a, b);
}

IRInstLShr *IRBuilder::CreateLShr(IRValue *a, IRValue *b)
{
	return add<IRInstLShr>(a, b);
}

IRInstAShr *IRBuilder::CreateAShr(IRValue *a, IRValue *b)
{
	return add<IRInstAShr>(a, b);
}

IRInstICmpSLT *IRBuilder::CreateICmpSLT(IRValue *a, IRValue *b)
{
	return addCmp<IRInstICmpSLT>(a, b);
}

IRInstICmpULT *IRBuilder::CreateICmpULT(IRValue *a, IRValue *b)
{
	return addCmp<IRInstICmpULT>(a, b);
}

IRInstFCmpULT *IRBuilder::CreateFCmpULT(IRValue *a, IRValue *b)
{
	return addCmp<IRInstFCmpULT>(a, b);
}

IRInstICmpSGT *IRBuilder::CreateICmpSGT(IRValue *a, IRValue *b)
{
	return addCmp<IRInstICmpSGT>(a, b);
}

IRInstICmpUGT *IRBuilder::CreateICmpUGT(IRValue *a, IRValue *b)
{
	return addCmp<IRInstICmpUGT>(a, b);
}

IRInstFCmpUGT *IRBuilder::CreateFCmpUGT(IRValue *a, IRValue *b)
{
	return addCmp<IRInstFCmpUGT>(a, b);
}

IRInstICmpSLE *IRBuilder::CreateICmpSLE(IRValue *a, IRValue *b)
{
	return addCmp<IRInstICmpSLE>(a, b);
}

IRInstICmpULE *IRBuilder::CreateICmpULE(IRValue *a, IRValue *b)
{
	return addCmp<IRInstICmpULE>(a, b);
}

IRInstFCmpULE *IRBuilder::CreateFCmpULE(IRValue *a, IRValue *b)
{
	return addCmp<IRInstFCmpULE>(a, b);
}

IRInstICmpSGE *IRBuilder::CreateICmpSGE(IRValue *a, IRValue *b)
{
	return addCmp<IRInstICmpSGE>(a, b);
}

IRInstICmpUGE *IRBuilder::CreateICmpUGE(IRValue *a, IRValue *b)
{
	return addCmp<IRInstICmpUGE>(a, b);
}

IRInstFCmpUGE *IRBuilder::CreateFCmpUGE(IRValue *a, IRValue *b)
{
	return addCmp<IRInstFCmpUGE>(a, b);
}

IRInstICmpEQ *IRBuilder::CreateICmpEQ(IRValue *a, IRValue *b)
{
	return addCmp<IRInstICmpEQ>(a, b);
}

IRInstFCmpUEQ *IRBuilder::CreateFCmpUEQ(IRValue *a, IRValue *b)
{
	return addCmp<IRInstFCmpUEQ>(a, b);
}

IRInstICmpNE *IRBuilder::CreateICmpNE(IRValue *a, IRValue *b)
{
	return addCmp<IRInstICmpNE>(a, b);
}

IRInstFCmpUNE *IRBuilder::CreateFCmpUNE(IRValue *a, IRValue *b)
{
	return addCmp<IRInstFCmpUNE>(a, b);
}

IRInstAnd *IRBuilder::CreateAnd(IRValue *a, IRValue *b)
{
	return add<IRInstAnd>(a, b);
}

IRInstOr *IRBuilder::CreateOr(IRValue *a, IRValue *b)
{
	return add<IRInstOr>(a, b);
}

IRInstXor *IRBuilder::CreateXor(IRValue *a, IRValue *b)
{
	return add<IRInstXor>(a, b);
}

IRInstBr *IRBuilder::CreateBr(IRBasicBlock *a)
{
	return add<IRInstBr>(a);
}

IRInstCondBr *IRBuilder::CreateCondBr(IRValue *a, IRBasicBlock *b, IRBasicBlock *c)
{
	return add<IRInstCondBr>(a, b, c);
}

IRInstRet *IRBuilder::CreateRet(IRValue *a)
{
	return add<IRInstRet>(a);
}

IRInstRetVoid *IRBuilder::CreateRetVoid()
{
	return add<IRInstRetVoid>();
}
