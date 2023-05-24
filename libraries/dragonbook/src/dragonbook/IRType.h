#pragma once

#include "IRContext.h"
#include "IRValue.h"

class IRContext;
class IRPointerType;

class IRType
{
public:
	virtual ~IRType() = default;

	IRPointerType *getPointerTo(IRContext *context);
	virtual IRType *getPointerElementType() { return nullptr; }

	virtual int getTypeAllocSize() = 0;

private:
	IRPointerType *pointer = nullptr;
};

class IRPointerType : public IRType
{
public:
	IRPointerType(IRType *elementType) : elementType(elementType) { }

	IRType *getPointerElementType() override { return elementType; }
	int getTypeAllocSize() override { return (int)sizeof(void*); }

	IRType *elementType = nullptr;
};

class IRVoidType : public IRType { int getTypeAllocSize() override { return 0; } };
class IRInt1Type : public IRType { int getTypeAllocSize() override { return (int)sizeof(bool); } };
class IRInt8Type : public IRType { int getTypeAllocSize() override { return (int)sizeof(uint8_t); } };
class IRInt16Type : public IRType { int getTypeAllocSize() override { return (int)sizeof(uint16_t); } };
class IRInt32Type : public IRType { int getTypeAllocSize() override { return (int)sizeof(uint32_t); } };
class IRInt64Type : public IRType { int getTypeAllocSize() override { return (int)sizeof(uint64_t); } };
class IRFloatType : public IRType { int getTypeAllocSize() override { return (int)sizeof(float); } };
class IRDoubleType : public IRType { int getTypeAllocSize() override { return (int)sizeof(double); } };

class IRStructType : public IRType
{
public:
	IRStructType(std::string name) : name(name) { }

	std::string name;
	std::vector<IRType *> elements;

	int getTypeAllocSize() override
	{
		int size = 0;
		for (IRType *element : elements)
		{
			size += (element->getTypeAllocSize() + 7) / 8 * 8;
		}
		return size;
	}
};

class IRFunctionType : public IRType
{
public:
	IRFunctionType(IRType *returnType, std::vector<IRType *> args) : returnType(returnType), args(args) { }

	IRType *returnType = nullptr;
	std::vector<IRType *> args;

	int getTypeAllocSize() override { return (int)sizeof(void*); }
};

inline IRPointerType *IRType::getPointerTo(IRContext *context)
{
	if (!pointer)
		pointer = context->newType<IRPointerType>(this);
	return pointer;
}
