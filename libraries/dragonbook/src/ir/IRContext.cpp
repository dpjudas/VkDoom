
#include "dragonbook/IRContext.h"
#include "dragonbook/IRType.h"
#include "dragonbook/IRFunction.h"
#include "dragonbook/IRBasicBlock.h"
#include "dragonbook/IRFunction.h"
#include "mc/MachineInst.h"
#include "mc/MachineInstSelection.h"
#include "mc/RegisterAllocator.h"
#include "mc/AssemblyWriter.h"

IRContext::IRContext()
{
	voidType = newType<IRVoidType>();
	int1Type = newType<IRInt1Type>();
	int8Type = newType<IRInt8Type>();
	int16Type = newType<IRInt16Type>();
	int32Type = newType<IRInt32Type>();
	int64Type = newType<IRInt64Type>();
	floatType = newType<IRFloatType>();
	doubleType = newType<IRDoubleType>();
}

IRContext::~IRContext()
{
}

std::string IRContext::getFunctionAssembly(IRFunction* func)
{
	MachineFunction* mcfunc = MachineInstSelection::codegen(func);
	RegisterAllocator::run(func->context, mcfunc);
	AssemblyWriter writer(mcfunc);
	writer.codegen();
	return writer.output.str();
}

IRFunction *IRContext::getFunction(const std::string &name)
{
	auto it = functions.find(name);
	return (it != functions.end()) ? it->second : nullptr;
}

IRGlobalVariable *IRContext::getNamedGlobal(const std::string &name)
{
	auto it = globalVars.find(name);
	return (it != globalVars.end()) ? it->second : nullptr;
}

void IRContext::addGlobalMapping(IRValue *value, void *nativeFunc)
{
	globalMappings[value] = nativeFunc;
}

IRFunctionType *IRContext::getFunctionType(IRType *returnType, std::vector<IRType *> args)
{
	for (IRFunctionType *type : functionTypes)
	{
		if (type->returnType == returnType && type->args.size() == args.size())
		{
			bool found = true;
			for (size_t i = 0; i < args.size(); i++)
			{
				if (args[i] != type->args[i])
				{
					found = false;
					break;
				}
			}
			if (found)
				return type;
		}
	}

	IRFunctionType *type = newType<IRFunctionType>(returnType, args);
	functionTypes.push_back(type);
	return type;
}

IRFunction *IRContext::createFunction(IRFunctionType *type, const std::string &name)
{
	IRFunction *func = newValue<IRFunction>(this, type, name);
	functions[name] = func;
	return func;
}

IRGlobalVariable *IRContext::createGlobalVariable(IRType *type, IRConstant *value, const std::string &name)
{
	IRGlobalVariable *var = newValue<IRGlobalVariable>(type->getPointerTo(this), value, name);
	globalVars[name] = var;
	return var;
}

IRStructType *IRContext::createStructType(const std::string &name)
{
	return newType<IRStructType>(name);
}

IRConstantStruct *IRContext::getConstantStruct(IRStructType *type, const std::vector<IRConstant *> &values)
{
	for (IRConstantStruct *value : constantStructs)
	{
		if (value->type == type && value->values.size() == values.size())
		{
			bool found = true;
			for (size_t i = 0; i < values.size(); i++)
			{
				if (values[i] != value->values[i])
				{
					found = false;
					break;
				}
			}
			if (found)
				return value;
		}
	}

	IRConstantStruct *value = newValue<IRConstantStruct>(type, values);
	constantStructs.push_back(value);
	return value;
}

IRConstantFP *IRContext::getConstantFloat(IRType *type, double value)
{
	// Since the constant can be NaN we have to use its binary value.
	union
	{
		double f;
		uint64_t i;
	} key;

	key.f = value;

	auto it = floatConstants.find({ type, key.i });
	if (it != floatConstants.end())
		return it->second;

	IRConstantFP* c = newValue<IRConstantFP>(type, value);
	floatConstants[{ type, key.i }] = c;
	return c;
}

IRConstantInt *IRContext::getConstantInt(IRType *type, uint64_t value)
{
	auto it = intConstants.find({ type, value });
	if (it != intConstants.end())
		return it->second;

	IRConstantInt *c = newValue<IRConstantInt>(type, value);
	intConstants[{ type, value }] = c;
	return c;
}

IRConstantInt *IRContext::getConstantInt(int32_t value)
{
	return getConstantInt(getInt32Ty(), value);
}

IRConstantInt *IRContext::getConstantIntTrue()
{
	return getConstantInt(getInt1Ty(), 1);
}

IRConstantInt *IRContext::getConstantIntFalse()
{
	return getConstantInt(getInt1Ty(), 0);
}

IRPointerType *IRContext::getVoidPtrTy()
{
	return getVoidTy()->getPointerTo(this);
}

IRPointerType *IRContext::getInt1PtrTy()
{
	return getInt1Ty()->getPointerTo(this);
}

IRPointerType *IRContext::getInt8PtrTy()
{
	return getInt8Ty()->getPointerTo(this);
}

IRPointerType *IRContext::getInt16PtrTy()
{
	return getInt16Ty()->getPointerTo(this);
}

IRPointerType *IRContext::getInt32PtrTy()
{
	return getInt32Ty()->getPointerTo(this);
}

IRPointerType *IRContext::getInt64PtrTy()
{
	return getInt64Ty()->getPointerTo(this);
}

IRPointerType *IRContext::getFloatPtrTy()
{
	return getFloatTy()->getPointerTo(this);
}

IRPointerType *IRContext::getDoublePtrTy()
{
	return getDoubleTy()->getPointerTo(this);
}
