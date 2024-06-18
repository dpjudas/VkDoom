#pragma once

#include "IRValue.h"
#include "IRType.h"
#include "IRInstVisitor.h"
#include <stdexcept>

class IRBasicBlock;

class IRInst : public IRValue
{
public:
	using IRValue::IRValue;

	virtual void visit(IRInstVisitor *visitor) = 0;

	std::string comment;
	int fileIndex = -1;
	int lineNumber = -1;
};

class IRInstUnary : public IRInst
{
public:
	IRInstUnary(IRValue *operand) : IRInst(operand->type), operand(operand) { }

	IRValue *operand;
};

class IRInstBinary : public IRInst
{
public:
	IRInstBinary(IRValue *operand1, IRValue *operand2) : IRInst(operand1->type), operand1(operand1), operand2(operand2) { }

	IRValue *operand1;
	IRValue *operand2;

protected:
	IRInstBinary(IRType* type, IRValue* operand1, IRValue* operand2) : IRInst(type), operand1(operand1), operand2(operand2) { }
};

class IRInstLoad : public IRInstUnary { public: IRInstLoad(IRValue *operand) : IRInstUnary(operand) { type = operand->type->getPointerElementType(); } void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstStore : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstAdd : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstSub : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFAdd : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFSub : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstNot : public IRInstUnary { public: using IRInstUnary::IRInstUnary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstNeg : public IRInstUnary { public: using IRInstUnary::IRInstUnary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFNeg : public IRInstUnary { public: using IRInstUnary::IRInstUnary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstMul : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFMul : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstSDiv : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstUDiv : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFDiv : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstSRem : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstURem : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstShl : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstLShr : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor* visitor) { visitor->inst(this); } };
class IRInstAShr : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstCmp : public IRInstBinary
{
public:
	IRInstCmp(IRType* type, IRValue* operand1, IRValue* operand2) : IRInstBinary(type, operand1, operand2) { }
};

class IRInstICmpSLT : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstICmpULT : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFCmpULT : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstICmpSGT : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstICmpUGT : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFCmpUGT : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstICmpSLE : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstICmpULE : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFCmpULE : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstICmpSGE : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstICmpUGE : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFCmpUGE : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstICmpEQ : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFCmpUEQ : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstICmpNE : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFCmpUNE : public IRInstCmp { public: using IRInstCmp::IRInstCmp; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstAnd : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstOr : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstXor : public IRInstBinary { public: using IRInstBinary::IRInstBinary; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstCast : public IRInst
{
public:
	IRInstCast(IRValue *value, IRType *type) : IRInst(type), value(value) { }

	IRValue *value;
};

class IRInstTrunc : public IRInstCast { public: using IRInstCast::IRInstCast; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstZExt : public IRInstCast { public: using IRInstCast::IRInstCast; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstSExt : public IRInstCast { public: using IRInstCast::IRInstCast; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFPTrunc : public IRInstCast { public: using IRInstCast::IRInstCast; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFPExt : public IRInstCast { public: using IRInstCast::IRInstCast; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFPToUI : public IRInstCast { public: using IRInstCast::IRInstCast; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstFPToSI : public IRInstCast { public: using IRInstCast::IRInstCast; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstUIToFP : public IRInstCast { public: using IRInstCast::IRInstCast; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstSIToFP : public IRInstCast { public: using IRInstCast::IRInstCast; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };
class IRInstBitCast : public IRInstCast { public: using IRInstCast::IRInstCast; void visit(IRInstVisitor *visitor) { visitor->inst(this); } };

class IRInstCall : public IRInst
{
public:
	IRInstCall(IRValue *func, const std::vector<IRValue *> &args) : IRInst(nullptr), func(func), args(args)
	{
		IRFunctionType *functype;
		if (dynamic_cast<IRPointerType*>(func->type))
		{
			functype = dynamic_cast<IRFunctionType*>(func->type->getPointerElementType());
		}
		else
		{
			functype = dynamic_cast<IRFunctionType*>(func->type);
		}
		type = functype->returnType;
	}

	void visit(IRInstVisitor *visitor) { visitor->inst(this); }

	IRValue *func;
	std::vector<IRValue *> args;
};

class IRInstGEP : public IRInst
{
public:
	IRInstGEP(IRContext *context, IRValue *ptr, const std::vector<IRValue *> &indices) : IRInst(nullptr), ptr(ptr), indices(indices)
	{
		struct ValueOffset
		{
			IRValue* index = nullptr;
			int scale = 0;
		};

		int offset = 0;
		std::vector<ValueOffset> valueOffsets;

		IRType *curType = ptr->type;
		for (IRValue *index : indices)
		{
			if (dynamic_cast<IRPointerType*>(curType))
			{
				curType = curType->getPointerElementType();
				int scale = curType->getTypeAllocSize();

				IRConstantInt* cindex = dynamic_cast<IRConstantInt*>(index);
				if (cindex)
				{
					offset += scale * (int)cindex->value;
				}
				else
				{
					valueOffsets.push_back({ index, scale });
				}
			}
			else
			{
				IRStructType *stype = dynamic_cast<IRStructType*>(curType);
				if (stype)
				{
					IRConstantInt *cindex = dynamic_cast<IRConstantInt*>(index);
					if (!cindex)
						throw std::runtime_error("Indexing into a structure must use a constant integer");

					int cindexval = (int)cindex->value;
					for (int i = 0; i < cindexval; i++)
					{
						offset += (stype->elements[i]->getTypeAllocSize() + 7) / 8 * 8;
					}
					curType = stype->elements[cindexval];
				}
				else
				{
					throw std::runtime_error("Invalid arguments to GEP instruction");
				}
			}
		}
		type = curType->getPointerTo(context);

		// Lower GEP:
		if (offset != 0 || !valueOffsets.empty())
		{
			IRType* int64Ty = context->getInt64Ty();
			IRValue* result = addInst<IRInstBitCast>(context, ptr, int64Ty); // To do: should be IRInstPtrToInt

			if (offset != 0)
			{
				result = addInst<IRInstAdd>(context, result, context->getConstantInt(int64Ty, offset));
			}

			for (const ValueOffset& valoffset : valueOffsets)
			{
				IRValue* index = valoffset.index;
				if (index->type != int64Ty)
					index = addInst<IRInstSExt>(context, index, int64Ty);
				if (valoffset.scale != 1)
					index = addInst<IRInstMul>(context, index, context->getConstantInt(int64Ty, valoffset.scale));
				result = addInst<IRInstAdd>(context, result, index);
			}

			addInst<IRInstBitCast>(context, result, type); // To do: should be IRInstIntToPtr
		}
	}

	void visit(IRInstVisitor *visitor) { visitor->inst(this); }

	IRValue *ptr;
	std::vector<IRValue *> indices;

	std::vector<IRInst*> instructions;

private:
	template<typename T, typename... Types>
	T* addInst(IRContext* context, Types&&... args)
	{
		T* inst = context->newValue<T>(std::forward<Types>(args)...);
		instructions.push_back(inst);
		return inst;
	}
};

class IRInstBasicBlockEnd : public IRInst
{
public:
	IRInstBasicBlockEnd() : IRInst(nullptr) { }
};

class IRInstBr : public IRInstBasicBlockEnd
{
public:
	IRInstBr(IRBasicBlock *bb) : bb(bb) { }

	void visit(IRInstVisitor *visitor) { visitor->inst(this); }

	IRBasicBlock *bb;
};

class IRInstCondBr : public IRInstBasicBlockEnd
{
public:
	IRInstCondBr(IRValue *condition, IRBasicBlock *bb1, IRBasicBlock *bb2) : condition(condition), bb1(bb1), bb2(bb2) { }

	void visit(IRInstVisitor *visitor) { visitor->inst(this); }

	IRValue *condition;
	IRBasicBlock *bb1;
	IRBasicBlock *bb2;
};

class IRInstRet : public IRInstBasicBlockEnd
{
public:
	IRInstRet(IRValue *operand) : operand(operand) { }

	void visit(IRInstVisitor *visitor) { visitor->inst(this); }

	IRValue *operand;
};

class IRInstRetVoid : public IRInstBasicBlockEnd
{
public:
	void visit(IRInstVisitor *visitor) { visitor->inst(this); }
};

class IRInstAlloca : public IRInst
{
public:
	IRInstAlloca(IRContext *context, IRType *type, IRValue *arraySize, const std::string &name) : IRInst(type->getPointerTo(context)), arraySize(arraySize), name(name) { }

	void visit(IRInstVisitor *visitor) { visitor->inst(this); }

	IRValue *arraySize;
	std::string name;
};

class IRInstPhi : public IRInst
{
public:
	IRInstPhi(IRType* type, const std::vector<std::pair<IRBasicBlock*, IRValue*>>& values) : IRInst(type), values(values) { }

	void visit(IRInstVisitor* visitor) { visitor->inst(this); }

	std::vector<std::pair<IRBasicBlock*, IRValue*>> values;
};
