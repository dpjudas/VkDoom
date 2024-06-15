#pragma once

#include "MachineInst.h"

class MachineInstSelection : IRInstVisitor
{
public:
	static MachineFunction *codegen(IRFunction* function);
	static MachineFunction* dumpinstructions(IRFunction* function);

private:
	MachineInstSelection(IRFunction* sfunc) : context(sfunc->context), sfunc(sfunc) { }

	void findMaxCallArgsSize();

	void inst(IRInstLoad* node) override;
	void inst(IRInstStore* node) override;
	void inst(IRInstAdd* node) override;
	void inst(IRInstSub* node) override;
	void inst(IRInstFAdd* node) override;
	void inst(IRInstFSub* node) override;
	void inst(IRInstNot* node) override;
	void inst(IRInstNeg* node) override;
	void inst(IRInstFNeg* node) override;
	void inst(IRInstMul* node) override;
	void inst(IRInstFMul* node) override;
	void inst(IRInstSDiv* node) override;
	void inst(IRInstUDiv* node) override;
	void inst(IRInstFDiv* node) override;
	void inst(IRInstSRem* node) override;
	void inst(IRInstURem* node) override;
	void inst(IRInstShl* node) override;
	void inst(IRInstLShr* node) override;
	void inst(IRInstAShr* node) override;
	void inst(IRInstICmpSLT* node) override;
	void inst(IRInstICmpULT* node) override;
	void inst(IRInstFCmpULT* node) override;
	void inst(IRInstICmpSGT* node) override;
	void inst(IRInstICmpUGT* node) override;
	void inst(IRInstFCmpUGT* node) override;
	void inst(IRInstICmpSLE* node) override;
	void inst(IRInstICmpULE* node) override;
	void inst(IRInstFCmpULE* node) override;
	void inst(IRInstICmpSGE* node) override;
	void inst(IRInstICmpUGE* node) override;
	void inst(IRInstFCmpUGE* node) override;
	void inst(IRInstICmpEQ* node) override;
	void inst(IRInstFCmpUEQ* node) override;
	void inst(IRInstICmpNE* node) override;
	void inst(IRInstFCmpUNE* node) override;
	void inst(IRInstAnd* node) override;
	void inst(IRInstOr* node) override;
	void inst(IRInstXor* node) override;
	void inst(IRInstTrunc* node) override;
	void inst(IRInstZExt* node) override;
	void inst(IRInstSExt* node) override;
	void inst(IRInstFPTrunc* node) override;
	void inst(IRInstFPExt* node) override;
	void inst(IRInstFPToUI* node) override;
	void inst(IRInstFPToSI* node) override;
	void inst(IRInstUIToFP* node) override;
	void inst(IRInstSIToFP* node) override;
	void inst(IRInstBitCast* node) override;
	void inst(IRInstCall* node) override;
	void inst(IRInstGEP* node) override;
	void inst(IRInstBr* node) override;
	void inst(IRInstCondBr* node) override;
	void inst(IRInstRet* node) override;
	void inst(IRInstRetVoid* node) override;
	void inst(IRInstAlloca* node) override;

	bool isConstant(IRValue* value) const { return dynamic_cast<IRConstant*>(value); }
	bool isConstantInt(IRValue* value) const { return dynamic_cast<IRConstantInt*>(value); }
	bool isConstantFP(IRValue* value) const { return dynamic_cast<IRConstantFP*>(value); }
	bool isFunctionArg(IRValue* value) const { return dynamic_cast<IRFunctionArg*>(value); }
	bool isGlobalVariable(IRValue* value) const { return dynamic_cast<IRGlobalVariable*>(value); }

	bool isVoid(IRType* type) const { return dynamic_cast<IRVoidType*>(type); }
	bool isInt1(IRType* type) const { return dynamic_cast<IRInt1Type*>(type); }
	bool isInt8(IRType* type) const { return dynamic_cast<IRInt8Type*>(type); }
	bool isInt16(IRType* type) const { return dynamic_cast<IRInt16Type*>(type); }
	bool isInt32(IRType* type) const { return dynamic_cast<IRInt32Type*>(type); }
	bool isInt64(IRType* type) const { return dynamic_cast<IRInt64Type*>(type); }
	bool isFloat(IRType* type) const { return dynamic_cast<IRFloatType*>(type); }
	bool isDouble(IRType* type) const { return dynamic_cast<IRDoubleType*>(type); }
	bool isPointer(IRType* type) const { return dynamic_cast<IRPointerType*>(type); }
	bool isFunction(IRType* type) const { return dynamic_cast<IRFunctionType*>(type); }
	bool isStruct(IRType* type) const { return dynamic_cast<IRStructType*>(type); }

	void callWin64(IRInstCall* node);
	void callUnix64(IRInstCall* node);

	void simpleCompareInst(IRInstBinary* node, MachineInstOpcode opSet, MachineInstOpcode opSet2 = MachineInstOpcode::nop, MachineInstOpcode opSet3 = MachineInstOpcode::nop);
	void simpleBinaryInst(IRInstBinary* node, const MachineInstOpcode* binaryOps);
	void shiftBinaryInst(IRInstBinary* node, const MachineInstOpcode* binaryOps);
	void divBinaryInst(IRInstBinary* node, const MachineInstOpcode* binaryOps, bool remainder, bool zeroext);

	void addDebugInfo(MachineInst* inst);
	void pushValueOperand(MachineInst* inst, IRValue* operand, int dataSizeType);
	void pushBBOperand(MachineInst* inst, IRBasicBlock* bb);

	void emitInst(MachineInstOpcode opcode, const MachineOperand& operand1, const MachineOperand& operand2, const MachineOperand& operand3);
	void emitInst(MachineInstOpcode opcode, const MachineOperand& operand1, IRValue* operand2, int dataSizeType, const MachineOperand& operand3);
	void emitInst(MachineInstOpcode opcode, const MachineOperand& operand1, const MachineOperand& operand2);
	void emitInst(MachineInstOpcode opcode, const MachineOperand& operand1, IRValue* operand2, int dataSizeType);
	void emitInst(MachineInstOpcode opcode, IRValue* operand1, int dataSizeType, const MachineOperand& operand2);
	void emitInst(MachineInstOpcode opcode, const MachineOperand &operand);
	void emitInst(MachineInstOpcode opcode, IRValue* operand, int dataSizeType);
	void emitInst(MachineInstOpcode opcode, IRBasicBlock* target);
	void emitInst(MachineInstOpcode opcode, MachineBasicBlock* target);

	int getDataSizeType(IRType* type);

	uint64_t getConstantValueInt(IRValue* value);
	float getConstantValueFloat(IRValue* value);
	double getConstantValueDouble(IRValue* value);

	MachineOperand newReg(IRValue* node);
	MachineOperand newConstant(uint64_t address);
	MachineOperand newConstant(float value);
	MachineOperand newConstant(double value);
	MachineOperand newConstant(const void *data, int size);
	MachineOperand newImm(uint64_t imm);
	MachineOperand newPhysReg(RegisterName name);
	MachineOperand newTempReg(MachineRegClass cls);

	int createVirtReg(MachineRegClass cls);

	IRContext* context;
	IRFunction* sfunc;
	MachineFunction* mfunc = nullptr;
	MachineBasicBlock* bb = nullptr;
	std::map<IRBasicBlock*, MachineBasicBlock*> bbMap;

	std::map<IRValue*, MachineOperand> instRegister;

	IRInst* debugInfoInst = nullptr;
};
