
#include "MachineInstSelection.h"
#include <algorithm>

MachineFunction* MachineInstSelection::codegen(IRFunction* sfunc)
{
	sfunc->sortBasicBlocks();

	MachineInstSelection selection(sfunc);
	selection.mfunc = sfunc->context->newMachineFunction(sfunc->name);
	selection.mfunc->type = dynamic_cast<IRFunctionType*>(sfunc->type);
	selection.mfunc->prolog = sfunc->context->newMachineBasicBlock();
	selection.mfunc->epilog = sfunc->context->newMachineBasicBlock();
	selection.mfunc->registers.push_back(MachineRegClass::gp); // rax
	selection.mfunc->registers.push_back(MachineRegClass::gp); // rcx
	selection.mfunc->registers.push_back(MachineRegClass::gp); // rdx
	selection.mfunc->registers.push_back(MachineRegClass::gp); // rbx
	selection.mfunc->registers.push_back(MachineRegClass::reserved); // rsp
	selection.mfunc->registers.push_back(MachineRegClass::gp); // rbp
	selection.mfunc->registers.push_back(MachineRegClass::gp); // rsi
	selection.mfunc->registers.push_back(MachineRegClass::gp); // rdi
	selection.mfunc->registers.push_back(MachineRegClass::gp); // r8
	selection.mfunc->registers.push_back(MachineRegClass::gp); // r9
	selection.mfunc->registers.push_back(MachineRegClass::gp); // r10
	selection.mfunc->registers.push_back(MachineRegClass::gp); // r11
	selection.mfunc->registers.push_back(MachineRegClass::gp); // r12
	selection.mfunc->registers.push_back(MachineRegClass::gp); // r13
	selection.mfunc->registers.push_back(MachineRegClass::gp); // r14
	selection.mfunc->registers.push_back(MachineRegClass::gp); // r15
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm0
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm1
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm2
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm3
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm4
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm5
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm6
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm7
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm8
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm9
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm10
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm11
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm12
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm13
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm14
	selection.mfunc->registers.push_back(MachineRegClass::xmm); // xmm15
	selection.mfunc->registers.resize((size_t)RegisterName::vregstart);

	selection.findMaxCallArgsSize();

	for (IRValue* value : sfunc->args)
	{
		selection.newReg(value);
	}

	for (size_t i = 0; i < sfunc->basicBlocks.size(); i++)
	{
		IRBasicBlock* bb = sfunc->basicBlocks[i];
		MachineBasicBlock* mbb = sfunc->context->newMachineBasicBlock();
		selection.mfunc->basicBlocks.push_back(mbb);
		selection.bbMap[bb] = mbb;
	}

	selection.bb = selection.bbMap[sfunc->basicBlocks[0]];
	for (size_t i = 0; i < sfunc->stackVars.size(); i++)
	{
		IRInstAlloca* node = sfunc->stackVars[i];

		uint64_t size = node->type->getPointerElementType()->getTypeAllocSize() * selection.getConstantValueInt(node->arraySize);
		size = (size + 15) / 16 * 16;

		MachineOperand dst = selection.newReg(node);
		selection.mfunc->stackvars.push_back({ dst.registerIndex, size, node->name });
	}

	for (size_t i = 0; i < sfunc->basicBlocks.size(); i++)
	{
		IRBasicBlock* bb = sfunc->basicBlocks[i];
		selection.bb = selection.bbMap[bb];
		for (IRInst* node : bb->code)
		{
			selection.debugInfoInst = node;
			node->visit(&selection);
		}
	}

	return selection.mfunc;
}

void MachineInstSelection::inst(IRInstLoad* node)
{
	static const MachineInstOpcode loadOps[] = { MachineInstOpcode::loadsd, MachineInstOpcode::loadss, MachineInstOpcode::load64, MachineInstOpcode::load32, MachineInstOpcode::load16, MachineInstOpcode::load8 };
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::movsd, MachineInstOpcode::movss, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };

	int dataSizeType = getDataSizeType(node->type);

	if (isConstantInt(node->operand))
	{
		auto srcreg = newTempReg(MachineRegClass::gp);
		emitInst(MachineInstOpcode::mov64, srcreg, node->operand, 2);
		emitInst(loadOps[dataSizeType], newReg(node), srcreg);
	}
	else
	{
		emitInst(loadOps[dataSizeType], newReg(node), node->operand, dataSizeType);
	}
}

void MachineInstSelection::inst(IRInstStore* node)
{
	static const MachineInstOpcode loadOps[] = { MachineInstOpcode::loadsd, MachineInstOpcode::loadss, MachineInstOpcode::load64, MachineInstOpcode::load32, MachineInstOpcode::load16, MachineInstOpcode::load8 };
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::movsd, MachineInstOpcode::movss, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };
	static const MachineInstOpcode storeOps[] = { MachineInstOpcode::storesd, MachineInstOpcode::storess, MachineInstOpcode::store64, MachineInstOpcode::store32, MachineInstOpcode::store16, MachineInstOpcode::store8 };

	MachineOperand dstreg;
	if (isConstantInt(node->operand2))
	{
		dstreg = newTempReg(MachineRegClass::gp);
		emitInst(MachineInstOpcode::mov64, dstreg, node->operand2, 2);
	}
	else
	{
		dstreg = instRegister[node->operand2];
	}

	int dataSizeType = getDataSizeType(node->operand2->type->getPointerElementType());

	bool needs64BitImm = (dataSizeType == 2) && isConstantInt(node->operand1) && (getConstantValueInt(node->operand1) < -0x7fffffff || getConstantValueInt(node->operand1) > 0x7fffffff);
	bool isMemToMem = isConstantFP(node->operand1) || isGlobalVariable(node->operand1);
	if (isMemToMem)
	{
		auto srcreg = newTempReg(dataSizeType < 2 ? MachineRegClass::xmm : MachineRegClass::gp);
		emitInst(loadOps[dataSizeType], srcreg, node->operand1, dataSizeType);
		emitInst(storeOps[dataSizeType], node->operand2, dataSizeType, srcreg);
	}
	else if (needs64BitImm)
	{
		auto srcreg = newTempReg(MachineRegClass::gp);
		emitInst(movOps[dataSizeType], srcreg, node->operand1, dataSizeType);
		emitInst(storeOps[dataSizeType], dstreg, srcreg);
	}
	else
	{
		emitInst(storeOps[dataSizeType], dstreg, node->operand1, dataSizeType);
	}
}

void MachineInstSelection::inst(IRInstAdd* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::add64, MachineInstOpcode::add32, MachineInstOpcode::add16, MachineInstOpcode::add8 };
	simpleBinaryInst(node, ops);
}

void MachineInstSelection::inst(IRInstSub* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::sub64, MachineInstOpcode::sub32, MachineInstOpcode::sub16, MachineInstOpcode::sub8 };
	simpleBinaryInst(node, ops);
}

void MachineInstSelection::inst(IRInstFAdd* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::addsd, MachineInstOpcode::addss, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop };
	simpleBinaryInst(node, ops);
}

void MachineInstSelection::inst(IRInstFSub* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::subsd, MachineInstOpcode::subss, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop };
	simpleBinaryInst(node, ops);
}

void MachineInstSelection::inst(IRInstNot* node)
{
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };
	static const MachineInstOpcode notOps[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::not64, MachineInstOpcode::not32, MachineInstOpcode::not16, MachineInstOpcode::not8 };

	int dataSizeType = getDataSizeType(node->type);
	auto dst = newReg(node);
	emitInst(movOps[dataSizeType], dst, node->operand, dataSizeType);
	emitInst(notOps[dataSizeType], dst);
}

void MachineInstSelection::inst(IRInstNeg* node)
{
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };
	static const MachineInstOpcode negOps[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::neg64, MachineInstOpcode::neg32, MachineInstOpcode::neg16, MachineInstOpcode::neg8 };

	int dataSizeType = getDataSizeType(node->type);
	auto dst = newReg(node);
	emitInst(movOps[dataSizeType], dst, node->operand, dataSizeType);
	emitInst(negOps[dataSizeType], dst);
}

void MachineInstSelection::inst(IRInstFNeg* node)
{
	static const MachineInstOpcode xorOps[] = { MachineInstOpcode::xorpd, MachineInstOpcode::xorps, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop };
	static const MachineInstOpcode subOps[] = { MachineInstOpcode::subsd, MachineInstOpcode::subss, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop };

	int dataSizeType = getDataSizeType(node->type);
	auto dst = newReg(node);
	emitInst(xorOps[dataSizeType], dst, dst);
	emitInst(subOps[dataSizeType], dst, node->operand, dataSizeType);
}

void MachineInstSelection::inst(IRInstMul* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::imul64, MachineInstOpcode::imul32, MachineInstOpcode::imul16, MachineInstOpcode::imul8 };
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };

	int dataSizeType = getDataSizeType(node->type);
	if (dataSizeType == 5) // 8 bit multiply can only happen in ax register
	{
		auto dst = newReg(node);
		emitInst(MachineInstOpcode::mov8, newPhysReg(RegisterName::rax), node->operand1, dataSizeType);
		emitInst(MachineInstOpcode::imul8, node->operand2, dataSizeType);
		emitInst(MachineInstOpcode::mov8, dst, newPhysReg(RegisterName::rax));
	}
	else
	{
		simpleBinaryInst(node, ops);
	}
}

void MachineInstSelection::inst(IRInstFMul* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::mulsd, MachineInstOpcode::mulss, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop };
	simpleBinaryInst(node, ops);
}

void MachineInstSelection::inst(IRInstSDiv* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::idiv64, MachineInstOpcode::idiv32, MachineInstOpcode::idiv16, MachineInstOpcode::idiv8 };
	divBinaryInst(node, ops, false, false);
}

void MachineInstSelection::inst(IRInstUDiv* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::div64, MachineInstOpcode::div32, MachineInstOpcode::div16, MachineInstOpcode::div8 };
	divBinaryInst(node, ops, false, true);
}

void MachineInstSelection::inst(IRInstFDiv* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::divsd, MachineInstOpcode::divss, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop };
	simpleBinaryInst(node, ops);
}

void MachineInstSelection::inst(IRInstSRem* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::idiv64, MachineInstOpcode::idiv32, MachineInstOpcode::idiv16, MachineInstOpcode::idiv8 };
	divBinaryInst(node, ops, true, false);
}

void MachineInstSelection::inst(IRInstURem* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::div64, MachineInstOpcode::div32, MachineInstOpcode::div16, MachineInstOpcode::div8 };
	divBinaryInst(node, ops, true, true);
}

void MachineInstSelection::inst(IRInstShl* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::shl64, MachineInstOpcode::shl32, MachineInstOpcode::shl16, MachineInstOpcode::shl8 };
	shiftBinaryInst(node, ops);
}

void MachineInstSelection::inst(IRInstLShr* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::shr64, MachineInstOpcode::shr32, MachineInstOpcode::shr16, MachineInstOpcode::shr8 };
	shiftBinaryInst(node, ops);
}

void MachineInstSelection::inst(IRInstAShr* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::sar64, MachineInstOpcode::sar32, MachineInstOpcode::sar16, MachineInstOpcode::sar8 };
	shiftBinaryInst(node, ops);
}

void MachineInstSelection::inst(IRInstICmpSLT* node)
{
	simpleCompareInst(node, MachineInstOpcode::setl);
}

void MachineInstSelection::inst(IRInstICmpULT* node)
{
	simpleCompareInst(node, MachineInstOpcode::setb);
}

void MachineInstSelection::inst(IRInstFCmpULT* node)
{
	simpleCompareInst(node, MachineInstOpcode::setb);
}

void MachineInstSelection::inst(IRInstICmpSGT* node)
{
	simpleCompareInst(node, MachineInstOpcode::setg);
}

void MachineInstSelection::inst(IRInstICmpUGT* node)
{
	simpleCompareInst(node, MachineInstOpcode::seta);
}

void MachineInstSelection::inst(IRInstFCmpUGT* node)
{
	simpleCompareInst(node, MachineInstOpcode::seta);
}

void MachineInstSelection::inst(IRInstICmpSLE* node)
{
	simpleCompareInst(node, MachineInstOpcode::setle);
}

void MachineInstSelection::inst(IRInstICmpULE* node)
{
	simpleCompareInst(node, MachineInstOpcode::setbe);
}

void MachineInstSelection::inst(IRInstFCmpULE* node)
{
	simpleCompareInst(node, MachineInstOpcode::setbe);
}

void MachineInstSelection::inst(IRInstICmpSGE* node)
{
	simpleCompareInst(node, MachineInstOpcode::setge);
}

void MachineInstSelection::inst(IRInstICmpUGE* node)
{
	simpleCompareInst(node, MachineInstOpcode::setae);
}

void MachineInstSelection::inst(IRInstFCmpUGE* node)
{
	simpleCompareInst(node, MachineInstOpcode::setae);
}

void MachineInstSelection::inst(IRInstICmpEQ* node)
{
	simpleCompareInst(node, MachineInstOpcode::sete);
}

void MachineInstSelection::inst(IRInstFCmpUEQ* node)
{
	simpleCompareInst(node, MachineInstOpcode::sete);
}

void MachineInstSelection::inst(IRInstICmpNE* node)
{
	simpleCompareInst(node, MachineInstOpcode::setne);
}

void MachineInstSelection::inst(IRInstFCmpUNE* node)
{
	simpleCompareInst(node, MachineInstOpcode::setne);
}

void MachineInstSelection::inst(IRInstAnd* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::and64, MachineInstOpcode::and32, MachineInstOpcode::and16, MachineInstOpcode::and8 };
	simpleBinaryInst(node, ops);
}

void MachineInstSelection::inst(IRInstOr* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::or64, MachineInstOpcode::or32, MachineInstOpcode::or16, MachineInstOpcode::or8 };
	simpleBinaryInst(node, ops);
}

void MachineInstSelection::inst(IRInstXor* node)
{
	static const MachineInstOpcode ops[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::xor64, MachineInstOpcode::xor32, MachineInstOpcode::xor16, MachineInstOpcode::xor8 };
	simpleBinaryInst(node, ops);
}

void MachineInstSelection::inst(IRInstTrunc* node)
{
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };

	int dataSizeType = getDataSizeType(node->type);

	auto dst = newReg(node);
	emitInst(movOps[dataSizeType], dst, node->value, dataSizeType);
}

void MachineInstSelection::inst(IRInstZExt* node)
{
	static const MachineInstOpcode movsxOps[] =
	{
		MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop,
		MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop,
		MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::mov64, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop,
		MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::mov32, MachineInstOpcode::mov32, MachineInstOpcode::nop, MachineInstOpcode::nop,
		MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::movzx16_64, MachineInstOpcode::movzx16_32, MachineInstOpcode::mov16, MachineInstOpcode::nop,
		MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::movzx8_64, MachineInstOpcode::movzx8_32, MachineInstOpcode::movzx8_16, MachineInstOpcode::mov8,
	};

	int dstDataSizeType = getDataSizeType(node->type);
	int srcDataSizeType = getDataSizeType(node->value->type);
	auto dst = newReg(node);

	if (dstDataSizeType == 2 && srcDataSizeType == 3)
	{
		emitInst(MachineInstOpcode::xor64, dst, dst);
	}

	emitInst(movsxOps[srcDataSizeType * 6 + dstDataSizeType], dst, node->value, srcDataSizeType);
}

void MachineInstSelection::inst(IRInstSExt* node)
{
	static const MachineInstOpcode movsxOps[] =
	{
		MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop,
		MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop,
		MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::mov64, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop,
		MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::movsx32_64, MachineInstOpcode::mov32, MachineInstOpcode::nop, MachineInstOpcode::nop,
		MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::movsx16_64, MachineInstOpcode::movsx16_32, MachineInstOpcode::mov16, MachineInstOpcode::nop,
		MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::movsx8_64, MachineInstOpcode::movsx8_32, MachineInstOpcode::movsx8_16, MachineInstOpcode::mov8,
	};

	int dstDataSizeType = getDataSizeType(node->type);
	int srcDataSizeType = getDataSizeType(node->value->type);
	auto dst = newReg(node);
	emitInst(movsxOps[srcDataSizeType * 6 + dstDataSizeType], dst, node->value, srcDataSizeType);
}

void MachineInstSelection::inst(IRInstFPTrunc* node)
{
	auto dst = newReg(node);
	emitInst(isConstantFP(node->value) ? MachineInstOpcode::movss : MachineInstOpcode::cvtsd2ss, dst, node->value, 1);
}

void MachineInstSelection::inst(IRInstFPExt* node)
{
	auto dst = newReg(node);
	emitInst(isConstantFP(node->value) ? MachineInstOpcode::movsd : MachineInstOpcode::cvtss2sd, dst, node->value, 0);
}

void MachineInstSelection::inst(IRInstFPToUI* node)
{
	if (isConstantFP(node->value))
	{
		static const MachineInstOpcode movOps[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };
		int dstDataSizeType = getDataSizeType(node->type);

		auto dst = newReg(node);
		emitInst(movOps[dstDataSizeType], dst, newImm((uint64_t)getConstantValueDouble(node->value)));
	}
	else
	{
		static const MachineInstOpcode cvtOps[] = { MachineInstOpcode::cvttsd2si, MachineInstOpcode::cvttsd2si, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop };
		int srcDataSizeType = getDataSizeType(node->value->type);

		auto dst = newReg(node);
		emitInst(cvtOps[srcDataSizeType], dst, node->value, srcDataSizeType);
	}
}

void MachineInstSelection::inst(IRInstFPToSI* node)
{
	if (isConstantFP(node->value))
	{
		static const MachineInstOpcode movOps[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };
		int dstDataSizeType = getDataSizeType(node->type);

		auto dst = newReg(node);
		emitInst(movOps[dstDataSizeType], dst, newImm((uint64_t)(int64_t)getConstantValueDouble(node->value)));
	}
	else
	{
		static const MachineInstOpcode cvtOps[] = { MachineInstOpcode::cvttsd2si, MachineInstOpcode::cvttss2si, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop };
		int srcDataSizeType = getDataSizeType(node->value->type);

		auto dst = newReg(node);
		emitInst(cvtOps[srcDataSizeType], dst, node->value, srcDataSizeType);
	}
}

void MachineInstSelection::inst(IRInstUIToFP* node)
{
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::movsd, MachineInstOpcode::movss, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };
	static const MachineInstOpcode cvtOps[] = { MachineInstOpcode::cvtsi2sd, MachineInstOpcode::cvtsi2sd, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop };
	int dstDataSizeType = getDataSizeType(node->type);
	int srcDataSizeType = getDataSizeType(node->value->type);

	if (isConstantInt(node->value))
	{
		auto dst = newReg(node);
		emitInst(movOps[dstDataSizeType], dst, node->value, dstDataSizeType);
	}
	else if (srcDataSizeType != 2) // zero extend required
	{
		auto tmp = newTempReg(MachineRegClass::gp);
		auto dst = newReg(node);

		emitInst(MachineInstOpcode::xor64, tmp, tmp);
		emitInst(movOps[srcDataSizeType], tmp, node->value, srcDataSizeType);
		emitInst(cvtOps[dstDataSizeType], dst, tmp);
	}
	else
	{
		auto dst = newReg(node);
		emitInst(cvtOps[dstDataSizeType], dst, node->value, dstDataSizeType);
	}
}

void MachineInstSelection::inst(IRInstSIToFP* node)
{
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::movsd, MachineInstOpcode::movss, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };
	static const MachineInstOpcode cvtOps[] = { MachineInstOpcode::cvtsi2sd, MachineInstOpcode::cvtsi2ss, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::nop };
	int dstDataSizeType = getDataSizeType(node->type);
	int srcDataSizeType = getDataSizeType(node->value->type);

	if (isConstantInt(node->value))
	{
		auto dst = newReg(node);
		emitInst(movOps[dstDataSizeType], dst, node->value, dstDataSizeType);
	}
	else if (srcDataSizeType != dstDataSizeType + 2) // sign extend required
	{
		static const int bits[] = { 64, 32, 64, 32, 16, 8 };
		int dstbits = bits[dstDataSizeType];
		int srcbits = bits[srcDataSizeType];
		int bitcount = dstbits - srcbits;
		auto count = newImm(bitcount);
		auto tmp = newTempReg(MachineRegClass::gp);
		auto dst = newReg(node);

		emitInst(movOps[srcDataSizeType], tmp, node->value, srcDataSizeType);
		emitInst((dstDataSizeType == 0) ? MachineInstOpcode::shl64 : MachineInstOpcode::shl32, tmp, count);
		emitInst((dstDataSizeType == 0) ? MachineInstOpcode::sar64 : MachineInstOpcode::sar32, tmp, count);
		emitInst(cvtOps[dstDataSizeType], dst, tmp);
	}
	else
	{
		auto dst = newReg(node);
		emitInst(cvtOps[dstDataSizeType], dst, node->value, dstDataSizeType);
	}
}

void MachineInstSelection::inst(IRInstBitCast* node)
{
	if (isConstant(node->value))
	{
		static const MachineInstOpcode movOps[] = { MachineInstOpcode::movsd, MachineInstOpcode::movss, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };

		int dataSizeType = getDataSizeType(node->value->type);
		auto dst = newReg(node);

		emitInst(movOps[dataSizeType], dst, node->value, dataSizeType);
	}
	else
	{
		instRegister[node] = instRegister[node->value];
	}
}

void MachineInstSelection::inst(IRInstCall* node)
{
#ifdef WIN32
	callWin64(node);
#else
	callUnix64(node);
#endif
}

void MachineInstSelection::inst(IRInstGEP* node)
{
	if (!node->instructions.empty())
	{
		for (auto inst : node->instructions)
			inst->visit(this);

		instRegister[node] = instRegister[node->instructions.back()];
	}
	else
	{
		auto dst = newReg(node);
		emitInst(MachineInstOpcode::mov64, dst, node->ptr, getDataSizeType(node->ptr->type));
	}
}

void MachineInstSelection::inst(IRInstBr* node)
{
	emitInst(MachineInstOpcode::jmp, node->bb);
}

void MachineInstSelection::inst(IRInstCondBr* node)
{
	if (isConstant(node->condition))
	{
		if (getConstantValueInt(node->condition))
			emitInst(MachineInstOpcode::jmp, node->bb1);
		else
			emitInst(MachineInstOpcode::jmp, node->bb2);
	}
	else
	{
		emitInst(MachineInstOpcode::cmp8, node->condition, 3, newImm(1));
		emitInst(MachineInstOpcode::jne, node->bb2);
		emitInst(MachineInstOpcode::jmp, node->bb1);
	}
}

void MachineInstSelection::inst(IRInstRet* node)
{
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::movsd, MachineInstOpcode::movss, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };

	int dataSizeType = getDataSizeType(static_cast<IRFunctionType*>(sfunc->type)->returnType);
	MachineOperand dst = newPhysReg(dataSizeType < 2 ? RegisterName::xmm0 : RegisterName::rax);

	emitInst(movOps[dataSizeType], dst, node->operand, dataSizeType);
	emitInst(MachineInstOpcode::jmp, mfunc->epilog);
}

void MachineInstSelection::inst(IRInstRetVoid* node)
{
	emitInst(MachineInstOpcode::jmp, mfunc->epilog);
}

void MachineInstSelection::inst(IRInstAlloca* node)
{
	uint64_t size = node->type->getPointerElementType()->getTypeAllocSize() * getConstantValueInt(node->arraySize);
	size = (size + 15) / 16 * 16;

	// sub rsp,size
	emitInst(MachineInstOpcode::sub64, newPhysReg(RegisterName::rsp), newImm(size));

	MachineOperand dst = newReg(node);
	MachineOperand src = newPhysReg(RegisterName::rsp);

	if (mfunc->maxCallArgsSize == 0)
	{
		// mov vreg,rsp
		emitInst(MachineInstOpcode::mov64, dst, src);
	}
	else
	{
		// lea vreg,ptr[rsp+maxCallArgsSize]
		emitInst(MachineInstOpcode::lea, dst, src, newImm(mfunc->maxCallArgsSize));
	}

	mfunc->dynamicStackAllocations = true;
	mfunc->registers[(int)RegisterName::rbp].cls = MachineRegClass::reserved;
}

int MachineInstSelection::getDataSizeType(IRType* type)
{
	if (isInt32(type))
		return 3;
	else if (isFloat(type))
		return 1;
	else if (isDouble(type))
		return 0;
	else if (isInt16(type))
		return 4;
	else if (isInt8(type) || isInt1(type))
		return 5;
	else // if (isInt64(type) || isPointer(type) || isFunction(type))
		return 2;
}

void MachineInstSelection::findMaxCallArgsSize()
{
	int callArgsSize = 4;
	for (size_t i = 0; i < sfunc->basicBlocks.size(); i++)
	{
		IRBasicBlock* bb = sfunc->basicBlocks[i];
		for (IRValue* value : bb->code)
		{
			IRInstCall* node = dynamic_cast<IRInstCall*>(value);
			if (node)
			{
				callArgsSize = std::max(callArgsSize, (int)node->args.size());
			}
		}
	}
	mfunc->maxCallArgsSize = callArgsSize * 8;
}

void MachineInstSelection::callWin64(IRInstCall* node)
{
	static const MachineInstOpcode loadOps[] = { MachineInstOpcode::loadsd, MachineInstOpcode::loadss, MachineInstOpcode::load64, MachineInstOpcode::load32, MachineInstOpcode::load16, MachineInstOpcode::load8 };
	static const MachineInstOpcode storeOps[] = { MachineInstOpcode::storesd, MachineInstOpcode::storess, MachineInstOpcode::store64, MachineInstOpcode::store32, MachineInstOpcode::store16, MachineInstOpcode::store8 };
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::movsd, MachineInstOpcode::movss, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };
	static const RegisterName regvars[] = { RegisterName::rcx, RegisterName::rdx, RegisterName::r8, RegisterName::r9 };

	// Move arguments into place
	for (size_t i = 0; i < node->args.size(); i++)
	{
		IRValue* arg = node->args[i];

		bool isMemSrc = isConstantFP(arg) || isGlobalVariable(arg);
		int dataSizeType = getDataSizeType(arg->type);
		bool isXmm = dataSizeType < 2;
		bool needs64BitImm = (dataSizeType == 2) && isConstantInt(arg) && (getConstantValueInt(arg) < -0x7fffffff || getConstantValueInt(arg) > 0x7fffffff);

		// First four go to registers
		if (i < 4)
		{
			MachineOperand dst;
			dst.type = MachineOperandType::reg;
			if (isXmm)
				dst.registerIndex = (int)RegisterName::xmm0 + (int)i;
			else
				dst.registerIndex = (int)regvars[i];

			emitInst(isMemSrc ? loadOps[dataSizeType] : movOps[dataSizeType], dst, arg, dataSizeType);
		}
		else
		{
			MachineOperand dst;
			dst.type = MachineOperandType::stackOffset;
			dst.stackOffset = (int)(i * 8);

			if (isMemSrc)
			{
				MachineOperand tmpreg = newPhysReg(isXmm ? RegisterName::xmm0 : RegisterName::rax);
				emitInst(loadOps[dataSizeType], tmpreg, arg, dataSizeType);
				emitInst(storeOps[dataSizeType], dst, tmpreg);
			}
			else if (needs64BitImm)
			{
				MachineOperand tmpreg = newPhysReg(RegisterName::rax);
				emitInst(movOps[dataSizeType], tmpreg, arg, dataSizeType);
				emitInst(storeOps[dataSizeType], dst, tmpreg);
			}
			else
			{
				emitInst(storeOps[dataSizeType], dst, arg, dataSizeType);
			}
		}
	}

	// Call the function
	MachineOperand target;
	IRFunction* func = dynamic_cast<IRFunction*>(node->func);
	if (func)
	{
		target.type = MachineOperandType::func;
		target.func = func;
	}
	else
	{
		target = newPhysReg(RegisterName::rax);
		emitInst(MachineInstOpcode::mov64, target, instRegister[node->func]);
	}
	emitInst(MachineInstOpcode::call, target);

	// Move return value to virtual register
	if (node->type != context->getVoidTy())
	{
		int dataSizeType = getDataSizeType(node->type);
		bool isXmm = dataSizeType < 2;

		MachineOperand src = newPhysReg(isXmm ? RegisterName::xmm0 : RegisterName::rax);
		auto dst = newReg(node);

		emitInst(movOps[dataSizeType], dst, src);
	}
}

void MachineInstSelection::callUnix64(IRInstCall* node)
{
	static const MachineInstOpcode loadOps[] = { MachineInstOpcode::loadsd, MachineInstOpcode::loadss, MachineInstOpcode::load64, MachineInstOpcode::load32, MachineInstOpcode::load16, MachineInstOpcode::load8 };
	static const MachineInstOpcode storeOps[] = { MachineInstOpcode::storesd, MachineInstOpcode::storess, MachineInstOpcode::store64, MachineInstOpcode::store32, MachineInstOpcode::store16, MachineInstOpcode::store8 };
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::movsd, MachineInstOpcode::movss, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };
	static const RegisterName regvars[] = { RegisterName::rdi, RegisterName::rsi, RegisterName::rdx, RegisterName::rcx, RegisterName::r8, RegisterName::r9 };

	const int numRegisterArgs = 6;
	const int numXmmRegisterArgs = 8;
	int nextRegisterArg = 0;
	int nextXmmRegisterArg = 0;

	// Move arguments into place
	for (size_t i = 0; i < node->args.size(); i++)
	{
		IRValue* arg = node->args[i];

		bool isMemSrc = isConstantFP(arg) || isGlobalVariable(arg);
		int dataSizeType = getDataSizeType(arg->type);
		bool isXmm = dataSizeType < 2;
		bool isRegisterArg = (isXmm && nextXmmRegisterArg < numXmmRegisterArgs) || (!isXmm && nextRegisterArg < numRegisterArgs);
		bool needs64BitImm = (dataSizeType == 2) && isConstantInt(arg) && (getConstantValueInt(arg) < -0x7fffffffffff || getConstantValueInt(arg) > 0x7fffffffffff);

		if (isRegisterArg)
		{
			MachineOperand dst;
			dst.type = MachineOperandType::reg;
			if (isXmm)
				dst.registerIndex = (int)RegisterName::xmm0 + (nextXmmRegisterArg++);
			else
				dst.registerIndex = (int)regvars[nextRegisterArg++];

			emitInst(isMemSrc ? loadOps[dataSizeType] : movOps[dataSizeType], dst, arg, dataSizeType);
		}
		else
		{
			MachineOperand dst;
			dst.type = MachineOperandType::stackOffset;
			dst.stackOffset = (int)(i * 8);

			if (isMemSrc)
			{
				MachineOperand tmpreg = newPhysReg(isXmm ? RegisterName::xmm0 : RegisterName::rax);
				emitInst(loadOps[dataSizeType], tmpreg, arg, dataSizeType);
				emitInst(storeOps[dataSizeType], dst, tmpreg);
			}
			else if (needs64BitImm)
			{
				MachineOperand tmpreg = newPhysReg(RegisterName::rax);
				emitInst(movOps[dataSizeType], tmpreg, arg, dataSizeType);
				emitInst(storeOps[dataSizeType], dst, tmpreg);
			}
			else
			{
				emitInst(storeOps[dataSizeType], dst, arg, dataSizeType);
			}
		}
	}

	// Call the function
	MachineOperand target;
	IRFunction* func = dynamic_cast<IRFunction*>(node->func);
	if (func)
	{
		target.type = MachineOperandType::func;
		target.func = func;
	}
	else
	{
		target = newPhysReg(RegisterName::rax);
		emitInst(MachineInstOpcode::mov64, target, instRegister[node->func]);
	}
	emitInst(MachineInstOpcode::call, target);

	// Move return value to virtual register
	if (node->type != context->getVoidTy())
	{
		int dataSizeType = getDataSizeType(node->type);
		bool isXmm = dataSizeType < 2;

		MachineOperand src = newPhysReg(isXmm ? RegisterName::xmm0 : RegisterName::rax);
		auto dst = newReg(node);

		emitInst(movOps[dataSizeType], dst, src);
	}
}

void MachineInstSelection::simpleCompareInst(IRInstBinary* node, MachineInstOpcode opSet)
{
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::movsd, MachineInstOpcode::movss, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };
	static const MachineInstOpcode cmpOps[] = { MachineInstOpcode::ucomisd, MachineInstOpcode::ucomiss, MachineInstOpcode::cmp64, MachineInstOpcode::cmp32, MachineInstOpcode::cmp16, MachineInstOpcode::cmp8 };

	int dataSizeType = getDataSizeType(node->operand1->type);

	// operand1 must be in a register:
	MachineOperand src1;
	if (isConstant(node->operand1))
	{
		src1 = newTempReg(dataSizeType < 2 ? MachineRegClass::xmm : MachineRegClass::gp);
		emitInst(movOps[dataSizeType], src1, node->operand1, dataSizeType);
	}
	else
	{
		src1 = instRegister[node->operand1];
	}

	// Create 8 bit register
	auto dst = newReg(node);

	// Perform comparison
	emitInst(cmpOps[dataSizeType], src1, node->operand2, dataSizeType);

	// Move result flag to register
	emitInst(opSet, dst);
}

void MachineInstSelection::simpleBinaryInst(IRInstBinary* node, const MachineInstOpcode* binaryOps)
{
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::movsd, MachineInstOpcode::movss, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };

	int dataSizeType = getDataSizeType(node->type);
	auto dst = newReg(node);

	// Move operand1 to dest
	emitInst(movOps[dataSizeType], dst, node->operand1, dataSizeType);

	// Apply operand2 to dest
	emitInst(binaryOps[dataSizeType], dst, node->operand2, dataSizeType);
}

void MachineInstSelection::shiftBinaryInst(IRInstBinary* node, const MachineInstOpcode* binaryOps)
{
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::movsd, MachineInstOpcode::movss, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };

	int dataSizeType = getDataSizeType(node->type);
	auto dst = newReg(node);

	if (isConstant(node->operand2))
	{
		// Move operand1 to dest
		emitInst(movOps[dataSizeType], dst, node->operand1, dataSizeType);

		// Apply operand2 to dest
		emitInst(binaryOps[dataSizeType], dst, node->operand2, dataSizeType);
	}
	else
	{
		// Move operand2 to rcx
		emitInst(movOps[dataSizeType], newPhysReg(RegisterName::rcx), node->operand2, dataSizeType);

		// Move operand1 to dest
		emitInst(movOps[dataSizeType], dst, node->operand1, dataSizeType);

		// Apply rcx to dest
		emitInst(binaryOps[dataSizeType], dst, newPhysReg(RegisterName::rcx));
	}
}

void MachineInstSelection::divBinaryInst(IRInstBinary* node, const MachineInstOpcode* binaryOps, bool remainder, bool zeroext)
{
	static const MachineInstOpcode movOps[] = { MachineInstOpcode::movsd, MachineInstOpcode::movss, MachineInstOpcode::mov64, MachineInstOpcode::mov32, MachineInstOpcode::mov16, MachineInstOpcode::mov8 };
	static const MachineInstOpcode sarOps[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::sar64, MachineInstOpcode::sar32, MachineInstOpcode::sar16, MachineInstOpcode::sar8 };
	static const MachineInstOpcode xorOps[] = { MachineInstOpcode::nop, MachineInstOpcode::nop, MachineInstOpcode::xor64, MachineInstOpcode::xor32, MachineInstOpcode::xor16, MachineInstOpcode::xor8 };
	static const int shiftcount[] = { 0, 0, 63, 31, 15, 7 };

	int dataSizeType = getDataSizeType(node->type);

	auto dst = newReg(node);

	// Move operand1 to rdx:rax
	emitInst(movOps[dataSizeType], newPhysReg(RegisterName::rax), node->operand1, dataSizeType);
	if (dataSizeType != 5)
	{
		if (zeroext)
		{
			emitInst(xorOps[dataSizeType], newPhysReg(RegisterName::rdx), newPhysReg(RegisterName::rdx));
		}
		else
		{
			emitInst(movOps[dataSizeType], newPhysReg(RegisterName::rdx), newPhysReg(RegisterName::rax));
			emitInst(sarOps[dataSizeType], newPhysReg(RegisterName::rdx), newImm(shiftcount[dataSizeType]));
		}
	}
	else
	{
		emitInst(zeroext ? MachineInstOpcode::movzx8_16 : MachineInstOpcode::movsx8_16, newPhysReg(RegisterName::rax), newPhysReg(RegisterName::rax));
	}

	// Divide
	if (isConstant(node->operand2) || isGlobalVariable(node->operand2))
	{
		emitInst(movOps[dataSizeType], dst, node->operand2, dataSizeType);
		emitInst(binaryOps[dataSizeType], dst);
	}
	else
	{
		emitInst(binaryOps[dataSizeType], node->operand2, dataSizeType);
	}

	// Move rax to dest
	if (dataSizeType != 5 || !remainder)
	{
		emitInst(movOps[dataSizeType], dst, newPhysReg(remainder ? RegisterName::rdx : RegisterName::rax));
	}
	else // for 8 bit the remainder is in ah
	{
		emitInst(MachineInstOpcode::shr16, newPhysReg(RegisterName::rax), newImm(8));
		emitInst(MachineInstOpcode::mov8, dst, newPhysReg(RegisterName::rax));
	}
}

void MachineInstSelection::addDebugInfo(MachineInst* inst)
{
	// Transfer debug info to first instruction emitted
	if (debugInfoInst)
	{
		inst->comment = debugInfoInst->comment;
		inst->fileIndex = debugInfoInst->fileIndex;
		inst->lineNumber = debugInfoInst->lineNumber;
		debugInfoInst = nullptr;
	}
}

void MachineInstSelection::emitInst(MachineInstOpcode opcode, const MachineOperand& operand1, const MachineOperand& operand2, const MachineOperand& operand3)
{
	auto inst = context->newMachineInst();
	addDebugInfo(inst);
	inst->opcode = opcode;
	inst->operands.push_back(operand1);
	inst->operands.push_back(operand2);
	inst->operands.push_back(operand3);
	bb->code.push_back(inst);
}

void MachineInstSelection::emitInst(MachineInstOpcode opcode, const MachineOperand& operand1, IRValue* operand2, int dataSizeType, const MachineOperand& operand3)
{
	auto inst = context->newMachineInst();
	addDebugInfo(inst);
	inst->opcode = opcode;
	inst->operands.push_back(operand1);
	pushValueOperand(inst, operand2, dataSizeType);
	inst->operands.push_back(operand3);
	bb->code.push_back(inst);
}

void MachineInstSelection::emitInst(MachineInstOpcode opcode, const MachineOperand& operand1, const MachineOperand& operand2)
{
	auto inst = context->newMachineInst();
	addDebugInfo(inst);
	inst->opcode = opcode;
	inst->operands.push_back(operand1);
	inst->operands.push_back(operand2);
	bb->code.push_back(inst);
}

void MachineInstSelection::emitInst(MachineInstOpcode opcode, const MachineOperand& operand1, IRValue* operand2, int dataSizeType)
{
	auto inst = context->newMachineInst();
	addDebugInfo(inst);
	inst->opcode = opcode;
	inst->operands.push_back(operand1);
	pushValueOperand(inst, operand2, dataSizeType);
	bb->code.push_back(inst);
}

void MachineInstSelection::emitInst(MachineInstOpcode opcode, IRValue* operand1, int dataSizeType, const MachineOperand& operand2)
{
	auto inst = context->newMachineInst();
	addDebugInfo(inst);
	inst->opcode = opcode;
	pushValueOperand(inst, operand1, dataSizeType);
	inst->operands.push_back(operand2);
	bb->code.push_back(inst);
}

void MachineInstSelection::emitInst(MachineInstOpcode opcode, const MachineOperand& operand)
{
	auto inst = context->newMachineInst();
	addDebugInfo(inst);
	inst->opcode = opcode;
	inst->operands.push_back(operand);
	bb->code.push_back(inst);
}

void MachineInstSelection::emitInst(MachineInstOpcode opcode, IRValue* operand, int dataSizeType)
{
	auto inst = context->newMachineInst();
	addDebugInfo(inst);
	inst->opcode = opcode;
	pushValueOperand(inst, operand, dataSizeType);
	bb->code.push_back(inst);
}

void MachineInstSelection::emitInst(MachineInstOpcode opcode, IRBasicBlock* target)
{
	auto inst = context->newMachineInst();
	addDebugInfo(inst);
	inst->opcode = opcode;
	pushBBOperand(inst, target);
	bb->code.push_back(inst);
}

void MachineInstSelection::emitInst(MachineInstOpcode opcode, MachineBasicBlock* target)
{
	MachineOperand operand;
	operand.type = MachineOperandType::basicblock;
	operand.bb = target;

	auto inst = context->newMachineInst();
	addDebugInfo(inst);
	inst->opcode = opcode;
	inst->operands.push_back(operand);
	bb->code.push_back(inst);
}

void MachineInstSelection::pushValueOperand(MachineInst* inst, IRValue* operand, int dataSizeType)
{
	if (isConstantFP(operand))
	{
		if (dataSizeType == 0)
			inst->operands.push_back(newConstant(getConstantValueDouble(operand)));
		else // if (dataSizeType == 1)
			inst->operands.push_back(newConstant(getConstantValueFloat(operand)));
	}
	else if (isConstantInt(operand))
	{
		inst->operands.push_back(newImm(getConstantValueInt(operand)));
	}
	else if (isGlobalVariable(operand))
	{
		MachineOperand mcoperand;
		mcoperand.type = MachineOperandType::global;
		mcoperand.global = static_cast<IRGlobalVariable*>(operand);
		inst->operands.push_back(mcoperand);
	}
	else
	{
		inst->operands.push_back(instRegister[operand]);
	}
}

void MachineInstSelection::pushBBOperand(MachineInst* inst, IRBasicBlock* bb)
{
	MachineOperand operand;
	operand.type = MachineOperandType::basicblock;
	operand.bb = bbMap[bb];
	inst->operands.push_back(operand);
}

MachineOperand MachineInstSelection::newReg(IRValue* node)
{
	MachineOperand operand;
	operand.type = MachineOperandType::reg;
	operand.registerIndex = createVirtReg(getDataSizeType(node->type) < 2 ? MachineRegClass::xmm : MachineRegClass::gp);
	instRegister[node] = operand;
	return operand;
}

MachineOperand MachineInstSelection::newPhysReg(RegisterName name)
{
	MachineOperand operand;
	operand.type = MachineOperandType::reg;
	operand.registerIndex = (int)name;
	return operand;
}

MachineOperand MachineInstSelection::newTempReg(MachineRegClass cls)
{
	MachineOperand operand;
	operand.type = MachineOperandType::reg;
	operand.registerIndex = createVirtReg(cls);
	return operand;
}

int MachineInstSelection::createVirtReg(MachineRegClass cls)
{
	int registerIndex = (int)mfunc->registers.size();
	mfunc->registers.push_back(cls);
	return registerIndex;
}

MachineOperand MachineInstSelection::newConstant(uint64_t address)
{
	return newConstant(&address, sizeof(uint64_t));
}

MachineOperand MachineInstSelection::newConstant(float value)
{
	return newConstant(&value, sizeof(float));
}

MachineOperand MachineInstSelection::newConstant(double value)
{
	return newConstant(&value, sizeof(double));
}

MachineOperand MachineInstSelection::newConstant(const void* data, int size)
{
	int index = (int)mfunc->constants.size();
	mfunc->constants.push_back({ data, size });

	MachineOperand operand;
	operand.type = MachineOperandType::constant;
	operand.constantIndex = index;
	return operand;
}

MachineOperand MachineInstSelection::newImm(uint64_t imm)
{
	MachineOperand operand;
	operand.type = MachineOperandType::imm;
	operand.immvalue = imm;
	return operand;
}

uint64_t MachineInstSelection::getConstantValueInt(IRValue* value)
{
	if (isConstantInt(value))
		return static_cast<IRConstantInt*>(value)->value;
	else
		return 0;
}

float MachineInstSelection::getConstantValueFloat(IRValue* value)
{
	if (isConstantFP(value))
		return static_cast<float>(static_cast<IRConstantFP*>(value)->value);
	else
		return 0.0f;
}

double MachineInstSelection::getConstantValueDouble(IRValue* value)
{
	if (isConstantFP(value))
		return static_cast<IRConstantFP*>(value)->value;
	else
		return 0.0;
}
