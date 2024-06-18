#pragma once

#include "dragonbook/IR.h"
#include "MachineInst.h"

class MachineCodeHolder;

class MachineCodeWriter
{
public:
	MachineCodeWriter(MachineCodeHolder *codeholder, MachineFunction* sfunc) : codeholder(codeholder), sfunc(sfunc) { }
	void codegen();

	MachineFunction* getFunc() const { return sfunc; }

private:
	void basicblock(MachineBasicBlock* bb);
	void opcode(MachineInst* inst);

	void nop(MachineInst* inst);
	void lea(MachineInst* inst);
	void loadss(MachineInst* inst);
	void loadsd(MachineInst* inst);
	void load64(MachineInst* inst);
	void load32(MachineInst* inst);
	void load16(MachineInst* inst);
	void load8(MachineInst* inst);
	void storess(MachineInst* inst);
	void storesd(MachineInst* inst);
	void store64(MachineInst* inst);
	void store32(MachineInst* inst);
	void store16(MachineInst* inst);
	void store8(MachineInst* inst);
	void movss(MachineInst* inst);
	void movsd(MachineInst* inst);
	void mov64(MachineInst* inst);
	void mov32(MachineInst* inst);
	void mov16(MachineInst* inst);
	void mov8(MachineInst* inst);
	void movsx8_16(MachineInst* inst);
	void movsx8_32(MachineInst* inst);
	void movsx8_64(MachineInst* inst);
	void movsx16_32(MachineInst* inst);
	void movsx16_64(MachineInst* inst);
	void movsx32_64(MachineInst* inst);
	void movzx8_16(MachineInst* inst);
	void movzx8_32(MachineInst* inst);
	void movzx8_64(MachineInst* inst);
	void movzx16_32(MachineInst* inst);
	void movzx16_64(MachineInst* inst);
	void addss(MachineInst* inst);
	void addsd(MachineInst* inst);
	void add64(MachineInst* inst);
	void add32(MachineInst* inst);
	void add16(MachineInst* inst);
	void add8(MachineInst* inst);
	void subss(MachineInst* inst);
	void subsd(MachineInst* inst);
	void sub64(MachineInst* inst);
	void sub32(MachineInst* inst);
	void sub16(MachineInst* inst);
	void sub8(MachineInst* inst);
	void not64(MachineInst* inst);
	void not32(MachineInst* inst);
	void not16(MachineInst* inst);
	void not8(MachineInst* inst);
	void neg64(MachineInst* inst);
	void neg32(MachineInst* inst);
	void neg16(MachineInst* inst);
	void neg8(MachineInst* inst);
	void shl64(MachineInst* inst);
	void shl32(MachineInst* inst);
	void shl16(MachineInst* inst);
	void shl8(MachineInst* inst);
	void shr64(MachineInst* inst);
	void shr32(MachineInst* inst);
	void shr16(MachineInst* inst);
	void shr8(MachineInst* inst);
	void sar64(MachineInst* inst);
	void sar32(MachineInst* inst);
	void sar16(MachineInst* inst);
	void sar8(MachineInst* inst);
	void and64(MachineInst* inst);
	void and32(MachineInst* inst);
	void and16(MachineInst* inst);
	void and8(MachineInst* inst);
	void or64(MachineInst* inst);
	void or32(MachineInst* inst);
	void or16(MachineInst* inst);
	void or8(MachineInst* inst);
	void xorpd(MachineInst* inst);
	void xorps(MachineInst* inst);
	void xor64(MachineInst* inst);
	void xor32(MachineInst* inst);
	void xor16(MachineInst* inst);
	void xor8(MachineInst* inst);
	void mulss(MachineInst* inst);
	void mulsd(MachineInst* inst);
	void imul64(MachineInst* inst);
	void imul32(MachineInst* inst);
	void imul16(MachineInst* inst);
	void imul8(MachineInst* inst);
	void divss(MachineInst* inst);
	void divsd(MachineInst* inst);
	void idiv64(MachineInst* inst);
	void idiv32(MachineInst* inst);
	void idiv16(MachineInst* inst);
	void idiv8(MachineInst* inst);
	void div64(MachineInst* inst);
	void div32(MachineInst* inst);
	void div16(MachineInst* inst);
	void div8(MachineInst* inst);
	void ucomisd(MachineInst* inst);
	void ucomiss(MachineInst* inst);
	void cmp64(MachineInst* inst);
	void cmp32(MachineInst* inst);
	void cmp16(MachineInst* inst);
	void cmp8(MachineInst* inst);
	void setl(MachineInst* inst);
	void setb(MachineInst* inst);
	void seta(MachineInst* inst);
	void setg(MachineInst* inst);
	void setle(MachineInst* inst);
	void setbe(MachineInst* inst);
	void setae(MachineInst* inst);
	void setge(MachineInst* inst);
	void sete(MachineInst* inst);
	void setne(MachineInst* inst);
	void setp(MachineInst* inst);
	void setnp(MachineInst* inst);
	void cvtsd2ss(MachineInst* inst);
	void cvtss2sd(MachineInst* inst);
	void cvttsd2si(MachineInst* inst);
	void cvttss2si(MachineInst* inst);
	void cvtsi2sd(MachineInst* inst);
	void cvtsi2ss(MachineInst* inst);
	void jmp(MachineInst* inst);
	void je(MachineInst* inst);
	void jne(MachineInst* inst);
	void call(MachineInst* inst);
	void ret(MachineInst* inst);
	void push(MachineInst* inst);
	void pop(MachineInst* inst);
	void movdqa(MachineInst* inst);

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

	struct OpFlags
	{
		enum
		{
			Rex = 1,
			RexW = 2,
			SizeOverride = 4, // 0x66
			NP = 8 // No additional prefixes allowed
		};
	};

	void emitInstZO(int flags, int opcode);
	void emitInstO(int flags, int opcode, MachineInst* inst);
	void emitInstOI(int flags, int opcode, int immsize, MachineInst* inst);
	void emitInstMI(int flags, int opcode, int immsize, MachineInst* inst, bool memptr = false);
	void emitInstMI(int flags, int opcode, int modopcode, int immsize, MachineInst* inst, bool memptr = false);
	void emitInstMR(int flags, int opcode, MachineInst* inst, bool memptr = false);
	void emitInstM(int flags, int opcode, MachineInst* inst, bool memptr = false);
	void emitInstM(int flags, int opcode, int modopcode, MachineInst* inst, bool memptr = false);
	void emitInstM(int flags, std::initializer_list<int> opcode, MachineInst* inst, bool memptr = false);
	void emitInstM(int flags, std::initializer_list<int> opcode, int modopcode, MachineInst* inst, bool memptr = false);
	void emitInstMC(int flags, int opcode, MachineInst* inst, bool memptr = false);
	void emitInstMC(int flags, int opcode, int modopcode, MachineInst* inst, bool memptr = false);
	void emitInstRM(int flags, int opcode, MachineInst* inst, bool memptr = false);
	void emitInstRM(int flags, std::initializer_list<int> opcode, MachineInst* inst, bool memptr = false);
	void emitInstRMI(int flags, int opcode, int immsize, MachineInst* inst, bool memptr = false);
	void emitInstSSE_RM(int flags, std::initializer_list<int> opcode, MachineInst* inst, bool memptr = false);
	void emitInstSSE_MR(int flags, std::initializer_list<int> opcode, MachineInst* inst, bool memptr = false);

	struct X64Instruction
	{
		// ModR/M:
		int mod = 0;
		int modreg = 0;
		int rm = 0;

		// SIB:
		int scale = 0;
		int index = 0;
		int base = 0;
		bool sib = false;

		// Disp:
		int32_t disp = 0;
		int dispsize = 0;

		// Immediate:
		uint64_t imm;
		int immsize = 0;
	};

	void setM(X64Instruction& x64inst, const MachineOperand& operand, bool memptr);
	void setI(X64Instruction& x64inst, int immsize, const MachineOperand& operand);
	void setR(X64Instruction& x64inst, const MachineOperand& operand);
	void setR(X64Instruction& x64inst, int modopcode);

	void writeInst(int flags, std::initializer_list<int> opcode, const X64Instruction& x64inst, MachineInst* debugInfo);
	void writeOpcode(int flags, std::initializer_list<int> opcode, int rexR, int rexX, int rexB);
	void writeModRM(int mod, int modreg, int rm);
	void writeSIB(int scale, int index, int base);
	void writeImm(int immsize, uint64_t value);

	static int getPhysReg(const MachineOperand& operand);

	MachineCodeHolder* codeholder;
	MachineFunction* sfunc;
	size_t funcBeginAddress = 0;
};
