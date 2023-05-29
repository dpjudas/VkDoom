
#include "dragonbook/IRInstValidator.h"
#include "dragonbook/IRInst.h"

void IRInstValidator::inst(IRInstLoad* node)
{
	if (!isPointer(node->operand->type))
		throw std::runtime_error("The pointer must be a pointer type");
}

void IRInstValidator::inst(IRInstStore* node)
{
	if (!isPointer(node->operand2->type))
		throw std::runtime_error("The pointer must be a pointer type");

	if (node->operand1->type != node->operand2->type->getPointerElementType())
		throw std::runtime_error("The pointer element type does not match the value type");
}

void IRInstValidator::inst(IRInstAdd* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstSub* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstFAdd* node)
{
	floatBinaryInst(node);
}

void IRInstValidator::inst(IRInstFSub* node)
{
	floatBinaryInst(node);
}

void IRInstValidator::inst(IRInstNot* node)
{
	intUnaryInst(node);
}

void IRInstValidator::inst(IRInstNeg* node)
{
	intUnaryInst(node);
}

void IRInstValidator::inst(IRInstFNeg* node)
{
	floatUnaryInst(node);
}

void IRInstValidator::inst(IRInstMul* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstFMul* node)
{
	floatBinaryInst(node);
}

void IRInstValidator::inst(IRInstSDiv* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstUDiv* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstFDiv* node)
{
	floatBinaryInst(node);
}

void IRInstValidator::inst(IRInstSRem* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstURem* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstShl* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstLShr* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstAShr* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstICmpSLT* node)
{
	intCompareInst(node);
}

void IRInstValidator::inst(IRInstICmpULT* node)
{
	intCompareInst(node);
}

void IRInstValidator::inst(IRInstFCmpULT* node)
{
	floatCompareInst(node);
}

void IRInstValidator::inst(IRInstICmpSGT* node)
{
	intCompareInst(node);
}

void IRInstValidator::inst(IRInstICmpUGT* node)
{
	intCompareInst(node);
}

void IRInstValidator::inst(IRInstFCmpUGT* node)
{
	floatCompareInst(node);
}

void IRInstValidator::inst(IRInstICmpSLE* node)
{
	intCompareInst(node);
}

void IRInstValidator::inst(IRInstICmpULE* node)
{
	intCompareInst(node);
}

void IRInstValidator::inst(IRInstFCmpULE* node)
{
	floatCompareInst(node);
}

void IRInstValidator::inst(IRInstICmpSGE* node)
{
	intCompareInst(node);
}

void IRInstValidator::inst(IRInstICmpUGE* node)
{
	intCompareInst(node);
}

void IRInstValidator::inst(IRInstFCmpUGE* node)
{
	floatCompareInst(node);
}

void IRInstValidator::inst(IRInstICmpEQ* node)
{
	intCompareInst(node);
}

void IRInstValidator::inst(IRInstFCmpUEQ* node)
{
	floatCompareInst(node);
}

void IRInstValidator::inst(IRInstICmpNE* node)
{
	intCompareInst(node);
}

void IRInstValidator::inst(IRInstFCmpUNE* node)
{
	floatCompareInst(node);
}

void IRInstValidator::inst(IRInstAnd* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstOr* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstXor* node)
{
	intBinaryInst(node);
}

void IRInstValidator::inst(IRInstTrunc* node)
{
	ValueType vt1 = getValueType(node->value->type);
	ValueType vt2 = getValueType(node->type);
	checkIntType(vt1);
	checkIntType(vt2);
	if (vt1 < vt2)
		throw std::runtime_error("The bit count for the target type must be less or equal to the source type");
}

void IRInstValidator::inst(IRInstZExt* node)
{
	ValueType vt1 = getValueType(node->value->type);
	ValueType vt2 = getValueType(node->type);
	checkIntType(vt1);
	checkIntType(vt2);
	if (vt1 > vt2)
		throw std::runtime_error("The bit count for the target type must be greater or equal to the source type");
}

void IRInstValidator::inst(IRInstSExt* node)
{
	ValueType vt1 = getValueType(node->value->type);
	ValueType vt2 = getValueType(node->type);
	checkIntType(vt1);
	checkIntType(vt2);
	if (vt1 > vt2)
		throw std::runtime_error("The bit count for the target type must be greater or equal to the source type");
}

void IRInstValidator::inst(IRInstFPTrunc* node)
{
	ValueType vt1 = getValueType(node->value->type);
	ValueType vt2 = getValueType(node->type);
	checkFloatType(vt1);
	checkFloatType(vt2);
	if (vt1 < vt2)
		throw std::runtime_error("The bit count for the target type must be less or equal to the source type");
}

void IRInstValidator::inst(IRInstFPExt* node)
{
	ValueType vt1 = getValueType(node->value->type);
	ValueType vt2 = getValueType(node->type);
	checkFloatType(vt1);
	checkFloatType(vt2);
	if (vt1 > vt2)
		throw std::runtime_error("The bit count for the target type must be greater or equal to the source type");
}

void IRInstValidator::inst(IRInstFPToUI* node)
{
	ValueType vt1 = getValueType(node->value->type);
	ValueType vt2 = getValueType(node->type);
	checkFloatType(vt1);
	checkIntType(vt2);
}

void IRInstValidator::inst(IRInstFPToSI* node)
{
	ValueType vt1 = getValueType(node->value->type);
	ValueType vt2 = getValueType(node->type);
	checkFloatType(vt1);
	checkIntType(vt2);
}

void IRInstValidator::inst(IRInstUIToFP* node)
{
	ValueType vt1 = getValueType(node->value->type);
	ValueType vt2 = getValueType(node->type);
	checkIntType(vt1);
	checkFloatType(vt2);
}

void IRInstValidator::inst(IRInstSIToFP* node)
{
	ValueType vt1 = getValueType(node->value->type);
	ValueType vt2 = getValueType(node->type);
	checkIntType(vt1);
	checkFloatType(vt2);
}

void IRInstValidator::inst(IRInstBitCast* node)
{
}

void IRInstValidator::inst(IRInstCall* node)
{
	IRFunctionType* functype;
	if (dynamic_cast<IRPointerType*>(node->func->type))
	{
		functype = dynamic_cast<IRFunctionType*>(node->func->type->getPointerElementType());
	}
	else
	{
		functype = dynamic_cast<IRFunctionType*>(node->func->type);
	}

	if (!functype)
		throw std::runtime_error("Invalid function argument");

	if (functype->args.size() != node->args.size())
		throw std::runtime_error("Arguments size mismatch for call");

	size_t count = functype->args.size();
	for (size_t i = 0; i < count; i++)
	{
		IRType* argtype = functype->args[i];
		IRValue* argvalue = node->args[i];
		if (argtype != argvalue->type)
			throw std::runtime_error("Argument type does not match");
	}
}

void IRInstValidator::inst(IRInstGEP* node)
{
	if (!isPointer(node->ptr->type))
		throw std::runtime_error("The pointer must be a pointer type");

	for (auto inst : node->instructions)
		inst->visit(this);
}

void IRInstValidator::inst(IRInstBr* node)
{
}

void IRInstValidator::inst(IRInstCondBr* node)
{
	if (!isInt1(node->condition->type))
		throw std::runtime_error("The branch condition must be an int1 type");
}

void IRInstValidator::inst(IRInstRet* node)
{
}

void IRInstValidator::inst(IRInstRetVoid* node)
{
}

void IRInstValidator::inst(IRInstAlloca* node)
{
	if (!isConstantInt(node->arraySize))
		throw std::runtime_error("The array size must be a constant");
}

void IRInstValidator::intUnaryInst(IRInstUnary* node)
{
	ValueType vt = getValueType(node->operand->type);
	checkIntType(vt);
}

void IRInstValidator::intBinaryInst(IRInstBinary* node)
{
	ValueType vt = getValueType(node->operand1->type);
	checkIntType(vt);
	checkSameType(node->operand1->type, node->operand2->type);
}

void IRInstValidator::intCompareInst(IRInstBinary* node)
{
	intBinaryInst(node);
}

void IRInstValidator::floatUnaryInst(IRInstUnary* node)
{
	ValueType vt = getValueType(node->operand->type);
	checkFloatType(vt);
}

void IRInstValidator::floatBinaryInst(IRInstBinary* node)
{
	ValueType vt = getValueType(node->operand1->type);
	checkFloatType(vt);
	checkSameType(node->operand1->type, node->operand2->type);
}

void IRInstValidator::floatCompareInst(IRInstBinary* node)
{
	floatBinaryInst(node);
}

void IRInstValidator::checkIntType(ValueType vt) const
{
	if (vt < ValueType::Int1 || vt > ValueType::Pointer)
		throw std::runtime_error("Not an integer type");
}

void IRInstValidator::checkFloatType(ValueType vt) const
{
	if (vt < ValueType::Float || vt > ValueType::Double)
		throw std::runtime_error("Not a floating point type");
}

void IRInstValidator::checkSameType(IRType* type1, IRType* type2) const
{
	if (type1 != type2)
		throw std::runtime_error("Both arguments must be of the same type");
}

IRInstValidator::ValueType IRInstValidator::getValueType(IRType* type) const
{
	// To do: get rid of all this stupid dynamic_cast stuff

	if (isVoid(type)) return ValueType::Void;
	else if (isInt1(type)) return ValueType::Int1;
	else if (isInt8(type)) return ValueType::Int8;
	else if (isInt16(type)) return ValueType::Int16;
	else if (isInt32(type)) return ValueType::Int32;
	else if (isInt64(type)) return ValueType::Int64;
	else if (isPointer(type)) return ValueType::Pointer;
	else if (isFloat(type)) return ValueType::Float;
	else if (isDouble(type)) return ValueType::Double;
	else /*if (isStruct(type))*/ return ValueType::Struct;
}
