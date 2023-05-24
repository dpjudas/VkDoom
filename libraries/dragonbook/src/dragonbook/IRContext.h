#pragma once

#include <cstdint>
#include <map>
#include <vector>
#include <memory>
#include "JITRuntime.h"

class IRType;
class IRPointerType;
class IRStructType;
class IRValue;
class IRConstant;
class IRConstantStruct;
class IRConstantFP;
class IRConstantInt;
class IRFunction;
class IRFunctionType;
class IRGlobalVariable;
class IRBasicBlock;
class MachineFunction;
class MachineBasicBlock;
class MachineInst;

class IRContext
{
public:
	IRContext();
	~IRContext();

	std::string getFunctionAssembly(IRFunction* func);

	IRFunction *getFunction(const std::string &name);
	IRGlobalVariable *getNamedGlobal(const std::string &name);
	void addGlobalMapping(IRValue *value, void *nativeFunc);

	IRFunctionType *getFunctionType(IRType *returnType, std::vector<IRType *> args);

	IRFunction *createFunction(IRFunctionType *type, const std::string &name);
	IRGlobalVariable *createGlobalVariable(IRType *type, IRConstant *value, const std::string &name);
	IRStructType *createStructType(const std::string &name = {});

	IRConstantStruct *getConstantStruct(IRStructType *type, const std::vector<IRConstant *> &values);
	IRConstantFP *getConstantFloat(IRType *type, double value);
	IRConstantInt *getConstantInt(IRType *type, uint64_t value);
	IRConstantInt *getConstantInt(IRType *type, uint32_t value) { return getConstantInt(type, (uint64_t)value); }
	IRConstantInt *getConstantInt(IRType *type, int64_t value) { return getConstantInt(type, (uint64_t)value); }
	IRConstantInt *getConstantInt(IRType *type, int32_t value) { return getConstantInt(type, (uint64_t)value); }
	IRConstantInt *getConstantInt(int32_t value);
	IRConstantInt *getConstantIntTrue();
	IRConstantInt *getConstantIntFalse();

	IRType *getVoidTy() { return voidType; }
	IRType *getInt1Ty() { return int1Type; }
	IRType *getInt8Ty() { return int8Type; }
	IRType *getInt16Ty() { return int16Type; }
	IRType *getInt32Ty() { return int32Type; }
	IRType *getInt64Ty() { return int64Type; }
	IRType *getFloatTy() { return floatType; }
	IRType *getDoubleTy() { return doubleType; }

	IRPointerType *getVoidPtrTy();
	IRPointerType *getInt1PtrTy();
	IRPointerType *getInt8PtrTy();
	IRPointerType *getInt16PtrTy();
	IRPointerType *getInt32PtrTy();
	IRPointerType *getInt64PtrTy();
	IRPointerType *getFloatPtrTy();
	IRPointerType *getDoublePtrTy();

	template<typename T, typename... Types>
	T* newType(Types&&... args)
	{
		allocatedTypes.push_back(std::make_unique<T>(std::forward<Types>(args)...));
		return static_cast<T*>(allocatedTypes.back().get());
	}

	template<typename T, typename... Types>
	T* newValue(Types&&... args)
	{
		allocatedValues.push_back(std::make_unique<T>(std::forward<Types>(args)...));
		return static_cast<T*>(allocatedValues.back().get());
	}

	template<typename... Types>
	IRBasicBlock* newBasicBlock(Types&&... args)
	{
		allocatedBBs.push_back(std::make_unique<IRBasicBlock>(std::forward<Types>(args)...));
		return allocatedBBs.back().get();
	}

	template<typename... Types>
	MachineFunction* newMachineFunction(Types&&... args)
	{
		allocatedMachineFunctions.push_back(std::make_unique<MachineFunction>(std::forward<Types>(args)...));
		return allocatedMachineFunctions.back().get();
	}

	template<typename... Types>
	MachineBasicBlock* newMachineBasicBlock(Types&&... args)
	{
		allocatedMachineBBs.push_back(std::make_unique<MachineBasicBlock>(std::forward<Types>(args)...));
		return allocatedMachineBBs.back().get();
	}

	template<typename... Types>
	MachineInst* newMachineInst(Types&&... args)
	{
		allocatedMachineInsts.push_back(std::make_unique<MachineInst>(std::forward<Types>(args)...));
		return allocatedMachineInsts.back().get();
	}

private:
	IRType *voidType = nullptr;
	IRType *int1Type = nullptr;
	IRType *int8Type = nullptr;
	IRType *int16Type = nullptr;
	IRType *int32Type = nullptr;
	IRType *int64Type = nullptr;
	IRType *floatType = nullptr;
	IRType *doubleType = nullptr;
	std::vector<IRFunctionType *> functionTypes;
	std::map<std::string, IRFunction *> functions;
	std::map<std::string, IRGlobalVariable *> globalVars;
	std::map<std::pair<IRType*, double>, IRConstantFP *> floatConstants;
	std::map<std::pair<IRType*, uint64_t>, IRConstantInt *> intConstants;
	std::vector<IRConstantStruct *> constantStructs;
	std::map<IRValue *, void *> globalMappings;

	std::vector<std::unique_ptr<IRType>> allocatedTypes;
	std::vector<std::unique_ptr<IRValue>> allocatedValues;
	std::vector<std::unique_ptr<IRBasicBlock>> allocatedBBs;
	std::vector<std::unique_ptr<MachineFunction>> allocatedMachineFunctions;
	std::vector<std::unique_ptr<MachineBasicBlock>> allocatedMachineBBs;
	std::vector<std::unique_ptr<MachineInst>> allocatedMachineInsts;

	friend class JITRuntime;
};
