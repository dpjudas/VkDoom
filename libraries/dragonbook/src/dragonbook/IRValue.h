#pragma once

#include <cstdint>
#include <vector>
#include <string>

class IRContext;
class IRType;
class IRStructType;

class IRValue
{
public:
	IRValue(IRType *type) : type(type) { }
	virtual ~IRValue() = default;

	IRType *type;
};

class IRConstant : public IRValue
{
public:
	using IRValue::IRValue;
};

class IRConstantStruct : public IRConstant
{
public:
	IRConstantStruct(IRType *type, const std::vector<IRConstant *> &values) : IRConstant(type), values(values) { }

	std::vector<IRConstant *> values;
};

class IRConstantInt : public IRConstant
{
public:
	IRConstantInt(IRType *type, uint64_t value) : IRConstant(type), value(value) { }

	uint64_t value;
};

class IRConstantFP : public IRConstant
{
public:
	IRConstantFP(IRType *type, double value) : IRConstant(type), value(value) { }

	double value;
};

class IRGlobalVariable : public IRValue
{
public:
	IRGlobalVariable(IRType *type, IRConstant *initialValue, const std::string &name) : IRValue(type), initialValue(initialValue), name(name) { }

	IRConstant *initialValue;
	std::string name;

	int globalsOffset = -1;
};

class IRFunctionArg : public IRValue
{
public:
	IRFunctionArg(IRType *type, int index) : IRValue(type), index(index) { }

	int index;
};
