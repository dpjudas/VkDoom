
#include "dragonbook/IR.h"
#include "dragonbook/JITRuntime.h"
#include <iostream>
#include <exception>
#include <functional>

class InstructionTest
{
public:
	InstructionTest(std::string name, std::function<void(IRContext*)> createCallback, std::function<bool(JITRuntime*)> runCallback) : name(name), createCallback(createCallback), runCallback(runCallback) { }

	void CreateFunc(IRContext* context) { createCallback(context);}
	bool RunTest(JITRuntime* runtime) { return runCallback(runtime); }
	
	std::string name;
	
private:
	std::function<void(IRContext*)> createCallback;
	std::function<bool(JITRuntime*)> runCallback;
};

class InstructionTester
{
public:
	void Test(std::string name, std::function<void(IRContext*)> createCallback, std::function<bool(JITRuntime*)> runCallback)
	{
		std::unique_ptr<InstructionTest> t(new InstructionTest(name, createCallback, runCallback));
		tests.push_back(std::move(t));
	}

	template<typename RetT, typename ArgT>
	void Convert(std::string name, std::function<IRValue* (IRBuilder*, IRValue*, IRType*)> codegen, std::function<RetT(ArgT)> cppversion)
	{
		auto create = [=](IRContext* context)
		{
			IRType* rettype = GetIRType<RetT>(context);
			IRType* type = GetIRType<ArgT>(context);
			IRFunction* func = context->createFunction(context->getFunctionType(rettype, { type }), name);

			IRBuilder builder;
			builder.SetInsertPoint(func->createBasicBlock("entry"));
			builder.CreateRet(codegen(&builder, func->args[0], rettype));
		};

		auto run = [=](JITRuntime* jit)
		{
			auto ptr = reinterpret_cast<RetT(*)(ArgT)>(jit->getPointerToFunction(name));
			for (int i = 0; i < 10; i++)
			{
				auto a = RandomValue<ArgT>();
				if ((ptr(a) != cppversion(a)))
				{
					ptr(a); // for debug breakpoint
					return false;
				}
			}
			return true;
		};

		Test(name, create, run);
	}

	template<typename T>
	void Unary(std::string name, std::function<IRValue* (IRBuilder*, IRValue*)> codegen, std::function<T(T)> cppversion)
	{
		auto create = [=](IRContext* context)
		{
			IRType* type = GetIRType<T>(context);
			IRFunction* func = context->createFunction(context->getFunctionType(type, { type, }), name);

			IRBuilder builder;
			builder.SetInsertPoint(func->createBasicBlock("entry"));
			builder.CreateRet(codegen(&builder, func->args[0]));
		};

		auto run = [=](JITRuntime* jit)
		{
			auto ptr = reinterpret_cast<T(*)(T)>(jit->getPointerToFunction(name));
			for (int i = 0; i < 10; i++)
			{
				auto a = RandomValue<T>();
				if ((ptr(a) != cppversion(a)))
				{
					ptr(a); // for debug breakpoint
					return false;
				}
			}
			return true;
		};

		Test(name, create, run);
	}

	template<typename T>
	void Binary(std::string name, std::function<IRValue*(IRBuilder*, IRValue*, IRValue*)> codegen, std::function<T(T, T)> cppversion)
	{
		auto create = [=](IRContext* context)
		{
			IRType* type = GetIRType<T>(context);
			IRFunction* func = context->createFunction(context->getFunctionType(type, { type, type }), name);
			
			IRBuilder builder;
			builder.SetInsertPoint(func->createBasicBlock("entry"));
			builder.CreateRet(codegen(&builder, func->args[0], func->args[1]));
		};

		auto run = [=](JITRuntime* jit)
		{
			auto ptr = reinterpret_cast<T(*)(T, T)>(jit->getPointerToFunction(name));
			for (int i = 0; i < 10; i++)
			{
				auto a = RandomValue<T>();
				auto b = RandomValue<T>();
				if (b == T()) b++; // to avoid division by zero
				if ((ptr(a, b) != cppversion(a, b)))
				{
					ptr(a, b); // for debug breakpoint
					return false;
				}
			}
			return true;
		};
		
		Test(name, create, run);
	}

	template<typename T>
	void Compare(std::string name, std::function<IRValue*(IRBuilder*, IRValue*, IRValue*)> codegen, std::function<bool(T, T)> cppversion)
	{
		auto create = [=](IRContext* context)
		{
			IRType* type = GetIRType<T>(context);
			IRFunction* func = context->createFunction(context->getFunctionType(context->getInt1Ty(), { type, type }), name);
			
			IRBuilder builder;
			builder.SetInsertPoint(func->createBasicBlock("entry"));
			builder.CreateRet(codegen(&builder, func->args[0], func->args[1]));
		};

		auto run = [=](JITRuntime* jit)
		{
			auto ptr = reinterpret_cast<int8_t(*)(T, T)>(jit->getPointerToFunction(name));
			for (int i = 0; i < 10; i++)
			{
				auto a = RandomValue<T>();
				auto b = RandomValue<T>();
				if (b == T()) b++; // to avoid division by zero
				if ((ptr(a, b) != (int8_t)cppversion(a, b)))
				{
					ptr(a, b); // for debug breakpoint
					return false;
				}
			}
			return true;
		};
		
		Test(name, create, run);
	}

	template<typename T>
	void NaNTest(std::string name, std::function<IRValue* (IRBuilder*, IRValue*, IRValue*)> codegen, std::function<bool(T, T)> cppversion)
	{
		auto create = [=](IRContext* context)
			{
				IRType* type = GetIRType<T>(context);
				IRFunction* func = context->createFunction(context->getFunctionType(context->getInt1Ty(), { type, type }), name);

				IRBuilder builder;
				builder.SetInsertPoint(func->createBasicBlock("entry"));
				builder.CreateRet(codegen(&builder, func->args[0], func->args[1]));
			};

		auto run = [=](JITRuntime* jit)
			{
				auto ptr = reinterpret_cast<int8_t(*)(T, T)>(jit->getPointerToFunction(name));
				for (int i = 0; i < 10; i++)
				{
					auto a = RandomValue<T>();
					auto b = std::numeric_limits<T>::quiet_NaN();
					if ((ptr(a, b) != (int8_t)cppversion(a, b)))
					{
						ptr(a, b); // for debug breakpoint
						return false;
					}
				}
				return true;
			};

		Test(name, create, run);
	}

	template<typename T>
	void Load(std::string name)
	{
		auto create = [=](IRContext* context)
		{
			IRType* type = GetIRType<T>(context);
			IRType* ptrtype = type->getPointerTo(context);
			IRFunction* func = context->createFunction(context->getFunctionType(type, { ptrtype }), name);

			IRBuilder builder;
			builder.SetInsertPoint(func->createBasicBlock("entry"));
			builder.CreateRet(builder.CreateLoad(func->args[0]));
		};

		auto run = [=](JITRuntime* jit)
		{
			auto ptr = reinterpret_cast<T(*)(T*)>(jit->getPointerToFunction(name));
			for (int i = 0; i < 10; i++)
			{
				auto a = RandomValue<T>();
				auto b = ptr(&a);
				if (a != b)
				{
					ptr(&a); // for debug breakpoint
					return false;
				}
			}
			return true;
		};

		Test(name, create, run);
	}

	template<typename T>
	void Store(std::string name)
	{
		auto create = [=](IRContext* context)
		{
			IRType* type = GetIRType<T>(context);
			IRType* ptrtype = type->getPointerTo(context);
			IRFunction* func = context->createFunction(context->getFunctionType(context->getVoidTy(), { type, ptrtype }), name);

			IRBuilder builder;
			builder.SetInsertPoint(func->createBasicBlock("entry"));
			builder.CreateStore(func->args[0], func->args[1]);
			builder.CreateRetVoid();
		};

		auto run = [=](JITRuntime* jit)
		{
			auto ptr = reinterpret_cast<void(*)(T, T*)>(jit->getPointerToFunction(name));
			for (int i = 0; i < 10; i++)
			{
				auto a = RandomValue<T>();
				auto b = RandomValue<T>();
				ptr(a, &b);
				if (a != b)
				{
					ptr(a, &b); // for debug breakpoint
					return false;
				}
			}
			return true;
		};

		Test(name, create, run);
	}

	template<typename T>
	void FPConstants(std::string name)
	{
		auto create = [=](IRContext* context)
		{
			IRType* type = GetIRType<T>(context);
			IRFunction* func = context->createFunction(context->getFunctionType(type, { type }), name);

			IRBuilder builder;
			builder.SetInsertPoint(func->createBasicBlock("entry"));
			auto result = builder.CreateFMul(func->args[0], context->getConstantFloat(type, 1.0));
			result = builder.CreateFMul(result, context->getConstantFloat(type, 2.0));
			result = builder.CreateFMul(result, context->getConstantFloat(type, 3.0));
			result = builder.CreateFMul(result, context->getConstantFloat(type, 4.0));
			builder.CreateRet(result);
		};

		auto run = [=](JITRuntime* jit)
		{
			auto ptr = reinterpret_cast<T(*)(T)>(jit->getPointerToFunction(name));

			if ((ptr(T(10)) != T(10) * T(1) * T(2) * T(3) * T(4)))
			{
				ptr(T(10)); // for debug breakpoint
				return false;
			}
			return true;
		};

		Test(name, create, run);
	}

	template<typename T>
	void RegisterSpillI(std::string name)
	{
		int random[10] = { 5,8,2,4,1,0,4,5,7,3 };
		auto create = [=](IRContext* context)
		{
			IRType* type = GetIRType<T>(context);
			IRFunction* func = context->createFunction(context->getFunctionType(type, { type, type, type, type }), name);

			IRValue* alloca0 = func->createAlloca(type, context->getConstantInt(1), "alloca0");
			IRValue* alloca1 = func->createAlloca(type, context->getConstantInt(1), "alloca1");
			IRValue* alloca2 = func->createAlloca(type, context->getConstantInt(1), "alloca2");
			IRValue* alloca3 = func->createAlloca(type, context->getConstantInt(1), "alloca3");

			IRBuilder builder;
			builder.SetInsertPoint(func->createBasicBlock("entry"));

			builder.CreateStore(func->args[0], alloca0);
			builder.CreateStore(func->args[1], alloca1);
			builder.CreateStore(func->args[2], alloca2);
			builder.CreateStore(func->args[3], alloca3);

			std::vector<IRValue*> values = { func->args[0], func->args[1], func->args[2], func->args[3] };
			for (int i = 0; i < 25; i++)
			{
				IRValue* result = values[0];
				for (size_t j = 1; j < values.size(); j++)
				{
					result = builder.CreateAdd(result, values[(j + random[j % 10]) % values.size()]);
				}
				values.push_back(result);
			}
			auto bb = func->createBasicBlock("bb");
			builder.CreateBr(bb);
			builder.SetInsertPoint(bb);
			for (int i = 0; i < 25; i++)
			{
				IRValue* result = values[0];
				for (size_t j = 1; j < values.size(); j++)
				{
					result = builder.CreateAdd(result, values[(j + random[j % 10]) % values.size()]);
				}
				values.push_back(result);
			}

			IRValue* result = values.back();
			result = builder.CreateAdd(result, builder.CreateLoad(alloca0));
			result = builder.CreateAdd(result, builder.CreateLoad(alloca1));
			result = builder.CreateAdd(result, builder.CreateLoad(alloca2));
			result = builder.CreateAdd(result, builder.CreateLoad(alloca3));
			builder.CreateRet(result);
		};

		auto run = [=](JITRuntime* jit)
		{
			auto ptr = reinterpret_cast<T(*)(T, T, T, T)>(jit->getPointerToFunction(name));

			std::vector<T> values = { T(1), T(2), T(3), T(4) };
			for (int i = 0; i < 50; i++)
			{
				T result = values[0];
				for (size_t j = 1; j < values.size(); j++)
				{
					result = result + values[(j + random[j % 10]) % values.size()];
				}
				values.push_back(result);
			}

			T result = values.back();
			result += T(1);
			result += T(2);
			result += T(3);
			result += T(4);

			if (ptr(T(1), T(2), T(3), T(4)) != result)
			{
				ptr(T(1), T(2), T(3), T(4)); // for debug breakpoint
				return false;
			}
			return true;
		};

		Test(name, create, run);
	}

	template<typename T>
	void RegisterSpillF(std::string name)
	{
		int random[10] = { 5,8,2,4,1,0,4,5,7,3 };
		auto create = [=](IRContext* context)
		{
			IRType* type = GetIRType<T>(context);
			IRFunction* func = context->createFunction(context->getFunctionType(type, { type, type, type, type }), name);

			IRValue* alloca0 = func->createAlloca(type, context->getConstantInt(1), "alloca0");
			IRValue* alloca1 = func->createAlloca(type, context->getConstantInt(1), "alloca1");
			IRValue* alloca2 = func->createAlloca(type, context->getConstantInt(1), "alloca2");
			IRValue* alloca3 = func->createAlloca(type, context->getConstantInt(1), "alloca3");

			IRBuilder builder;
			builder.SetInsertPoint(func->createBasicBlock("entry"));

			builder.CreateStore(func->args[0], alloca0);
			builder.CreateStore(func->args[1], alloca1);
			builder.CreateStore(func->args[2], alloca2);
			builder.CreateStore(func->args[3], alloca3);

			std::vector<IRValue*> values = { func->args[0], func->args[1], func->args[2], func->args[3] };
			for (int i = 0; i < 25; i++)
			{
				IRValue* result = values[0];
				for (size_t j = 1; j < values.size(); j++)
				{
					result = builder.CreateFAdd(result, values[(j + random[j%10])%values.size()]);
				}
				values.push_back(result);
			}
			auto bb = func->createBasicBlock("bb");
			builder.CreateBr(bb);
			builder.SetInsertPoint(bb);
			for (int i = 0; i < 25; i++)
			{
				IRValue* result = values[0];
				for (size_t j = 1; j < values.size(); j++)
				{
					result = builder.CreateFAdd(result, values[(j + random[j % 10]) % values.size()]);
				}
				values.push_back(result);
			}

			IRValue* result = values.back();
			result = builder.CreateFAdd(result, builder.CreateLoad(alloca0));
			result = builder.CreateFAdd(result, builder.CreateLoad(alloca1));
			result = builder.CreateFAdd(result, builder.CreateLoad(alloca2));
			result = builder.CreateFAdd(result, builder.CreateLoad(alloca3));
			builder.CreateRet(result);
		};

		auto run = [=](JITRuntime* jit)
		{
			auto ptr = reinterpret_cast<T(*)(T,T,T,T)>(jit->getPointerToFunction(name));

			std::vector<T> values = { T(1), T(2), T(3), T(4) };
			for (int i = 0; i < 50; i++)
			{
				T result = values[0];
				for (size_t j = 1; j < values.size(); j++)
				{
					result = result + values[(j + random[j % 10]) % values.size()];
				}
				values.push_back(result);
			}

			T result = values.back();
			result += T(1);
			result += T(2);
			result += T(3);
			result += T(4);

			if (ptr(T(1), T(2), T(3), T(4)) != result)
			{
				ptr(T(1), T(2), T(3), T(4)); // for debug breakpoint
				return false;
			}
			return true;
		};

		Test(name, create, run);
	}

	void Run()
	{
		std::cout << "Creating tests" << std::endl;

		IRContext context;
		for (auto& test : tests)
		{
			test->CreateFunc(&context);
		}
		
		JITRuntime jit;
		jit.add(&context);

		std::cout << "Running tests" << std::endl;

		for (auto& test : tests)
		{
			bool result = test->RunTest(&jit);
			if (!result)
			{
				std::cout << test->name.c_str() << " failed" << std::endl;
			}
		}

		std::cout << "All tests done" << std::endl;
	}

private:
	template<typename T> IRType* GetIRType(IRContext* ircontext) { static_assert(std::is_pointer<T>::value, "Unsupported type"); return ircontext->getInt8PtrTy(); }
	template<> IRType* GetIRType<void>(IRContext* ircontext) { return ircontext->getVoidTy(); }
	template<> IRType* GetIRType<int8_t>(IRContext* ircontext) { return ircontext->getInt8Ty(); }
	template<> IRType* GetIRType<uint8_t>(IRContext* ircontext) { return ircontext->getInt8Ty(); }
	template<> IRType* GetIRType<int16_t>(IRContext* ircontext) { return ircontext->getInt16Ty(); }
	template<> IRType* GetIRType<uint16_t>(IRContext* ircontext) { return ircontext->getInt16Ty(); }
	template<> IRType* GetIRType<int32_t>(IRContext* ircontext) { return ircontext->getInt32Ty(); }
	template<> IRType* GetIRType<uint32_t>(IRContext* ircontext) { return ircontext->getInt32Ty(); }
	template<> IRType* GetIRType<int64_t>(IRContext* ircontext) { return ircontext->getInt64Ty(); }
	template<> IRType* GetIRType<uint64_t>(IRContext* ircontext) { return ircontext->getInt64Ty(); }
	template<> IRType* GetIRType<float>(IRContext* ircontext) { return ircontext->getFloatTy(); }
	template<> IRType* GetIRType<double>(IRContext* ircontext) { return ircontext->getDoubleTy(); }

	template<typename T> T RandomValue() { return (T)rand(); }
	template<> int8_t RandomValue<int8_t>() { return rand() >> 8; }
	template<> uint8_t RandomValue<uint8_t>() { return rand() >> 8; }
	template<> int16_t RandomValue<int16_t>() { return rand(); }
	template<> uint16_t RandomValue<uint16_t>() { return rand(); }
	template<> int32_t RandomValue<int32_t>() { return ((int32_t)rand()) << 16; }
	template<> uint32_t RandomValue<uint32_t>() { return ((uint32_t)rand()) << 16; }
	template<> int64_t RandomValue<int64_t>() { return ((int64_t)rand()) << 32; }
	template<> uint64_t RandomValue<uint64_t>() { return ((uint64_t)rand()) << 32; }
	template<> float RandomValue<float>() { return (float)rand() + 0.5f; }
	template<> double RandomValue<double>() { return (double)rand() + 0.5; }

	std::vector<std::unique_ptr<InstructionTest>> tests;
};

int main(int argc, char** argv)
{
	try
	{
		InstructionTester tester;

		tester.Unary<int8_t>("not_int8", [](auto cc, auto a) { return cc->CreateNot(a); }, [](int8_t a) { return ~a; });
		tester.Unary<int8_t>("neg_int8", [](auto cc, auto a) { return cc->CreateNeg(a); }, [](int8_t a) { return -a; });
		tester.Unary<int16_t>("not_int16", [](auto cc, auto a) { return cc->CreateNot(a); }, [](int16_t a) { return ~a; });
		tester.Unary<int16_t>("neg_int16", [](auto cc, auto a) { return cc->CreateNeg(a); }, [](int16_t a) { return -a; });
		tester.Unary<int32_t>("not_int32", [](auto cc, auto a) { return cc->CreateNot(a); }, [](int32_t a) { return ~a; });
		tester.Unary<int32_t>("neg_int32", [](auto cc, auto a) { return cc->CreateNeg(a); }, [](int32_t a) { return -a; });
		tester.Unary<int64_t>("not_int64", [](auto cc, auto a) { return cc->CreateNot(a); }, [](int64_t a) { return ~a; });
		tester.Unary<int64_t>("neg_int64", [](auto cc, auto a) { return cc->CreateNeg(a); }, [](int64_t a) { return -a; });
		tester.Unary<float>("fneg_float", [](auto cc, auto a) { return cc->CreateFNeg(a); }, [](float a) { return -a; });
		tester.Unary<double>("fneg_double", [](auto cc, auto a) { return cc->CreateFNeg(a); }, [](double a) { return -a; });

		tester.Binary<int8_t>("add_int8", [](auto cc, auto a, auto b) { return cc->CreateAdd(a, b); }, [](int8_t a, int8_t b) { return a + b; });
		tester.Binary<int8_t>("sub_int8", [](auto cc, auto a, auto b) { return cc->CreateSub(a, b); }, [](int8_t a, int8_t b) { return a - b; });
		tester.Binary<int8_t>("mul_int8", [](auto cc, auto a, auto b) { return cc->CreateMul(a, b); }, [](int8_t a, int8_t b) { return a * b; });
		tester.Binary<int8_t>("sdiv_int8", [](auto cc, auto a, auto b) { return cc->CreateSDiv(a, b); }, [](int8_t a, int8_t b) { return a / b; });
		tester.Binary<int8_t>("udiv_int8", [](auto cc, auto a, auto b) { return cc->CreateUDiv(a, b); }, [](int8_t a, int8_t b) { return ((uint8_t)a) / (uint8_t)b; });
		tester.Binary<int8_t>("srem_int8", [](auto cc, auto a, auto b) { return cc->CreateSRem(a, b); }, [](int8_t a, int8_t b) { return a % b; });
		tester.Binary<int8_t>("urem_int8", [](auto cc, auto a, auto b) { return cc->CreateURem(a, b); }, [](int8_t a, int8_t b) { return ((uint8_t)a) % (uint8_t)b; });
		tester.Binary<int8_t>("and_int8", [](auto cc, auto a, auto b) { return cc->CreateAnd(a, b); }, [](int8_t a, int8_t b) { return a & b; });
		tester.Binary<int8_t>("or_int8", [](auto cc, auto a, auto b) { return cc->CreateOr(a, b); }, [](int8_t a, int8_t b) { return a | b; });
		tester.Binary<int8_t>("xor_int8", [](auto cc, auto a, auto b) { return cc->CreateXor(a, b); }, [](int8_t a, int8_t b) { return a ^ b; });
		tester.Binary<int8_t>("shl_int8", [](auto cc, auto a, auto b) { return cc->CreateShl(a, b); }, [](int8_t a, int8_t b) { return a << b; });
		tester.Binary<int8_t>("lshr_int8", [](auto cc, auto a, auto b) { return cc->CreateLShr(a, b); }, [](int8_t a, int8_t b) { return ((uint8_t)a) >> (uint8_t)b; });
		tester.Binary<int8_t>("ashr_int8", [](auto cc, auto a, auto b) { return cc->CreateAShr(a, b); }, [](int8_t a, int8_t b) { return a >> b; });

		tester.Binary<int16_t>("add_int16", [](auto cc, auto a, auto b) { return cc->CreateAdd(a, b); }, [](int16_t a, int16_t b) { return a + b; });
		tester.Binary<int16_t>("sub_int16", [](auto cc, auto a, auto b) { return cc->CreateSub(a, b); }, [](int16_t a, int16_t b) { return a - b; });
		tester.Binary<int16_t>("mul_int16", [](auto cc, auto a, auto b) { return cc->CreateMul(a, b); }, [](int16_t a, int16_t b) { return a * b; });
		tester.Binary<int16_t>("sdiv_int16", [](auto cc, auto a, auto b) { return cc->CreateSDiv(a, b); }, [](int16_t a, int16_t b) { return a / b; });
		tester.Binary<int16_t>("udiv_int16", [](auto cc, auto a, auto b) { return cc->CreateUDiv(a, b); }, [](int16_t a, int16_t b) { return ((uint16_t)a) / (uint16_t)b; });
		tester.Binary<int16_t>("srem_int16", [](auto cc, auto a, auto b) { return cc->CreateSRem(a, b); }, [](int16_t a, int16_t b) { return a % b; });
		tester.Binary<int16_t>("urem_int16", [](auto cc, auto a, auto b) { return cc->CreateURem(a, b); }, [](int16_t a, int16_t b) { return ((uint16_t)a) % (uint16_t)b; });
		tester.Binary<int16_t>("and_int16", [](auto cc, auto a, auto b) { return cc->CreateAnd(a, b); }, [](int16_t a, int16_t b) { return a & b; });
		tester.Binary<int16_t>("or_int16", [](auto cc, auto a, auto b) { return cc->CreateOr(a, b); }, [](int16_t a, int16_t b) { return a | b; });
		tester.Binary<int16_t>("xor_int16", [](auto cc, auto a, auto b) { return cc->CreateXor(a, b); }, [](int16_t a, int16_t b) { return a ^ b; });
		tester.Binary<int16_t>("shl_int16", [](auto cc, auto a, auto b) { return cc->CreateShl(a, b); }, [](int16_t a, int16_t b) { return a << b; });
		tester.Binary<int16_t>("lshr_int16", [](auto cc, auto a, auto b) { return cc->CreateLShr(a, b); }, [](int16_t a, int16_t b) { return ((uint16_t)a) >> (uint16_t)b; });
		tester.Binary<int16_t>("ashr_int16", [](auto cc, auto a, auto b) { return cc->CreateAShr(a, b); }, [](int16_t a, int16_t b) { return a >> b; });

		tester.Binary<int32_t>("add_int32", [](auto cc, auto a, auto b) { return cc->CreateAdd(a, b); }, [](int32_t a, int32_t b) { return a + b; });
		tester.Binary<int32_t>("sub_int32", [](auto cc, auto a, auto b) { return cc->CreateSub(a, b); }, [](int32_t a, int32_t b) { return a - b; });
		tester.Binary<int32_t>("mul_int32", [](auto cc, auto a, auto b) { return cc->CreateMul(a, b); }, [](int32_t a, int32_t b) { return a * b; });
		tester.Binary<int32_t>("sdiv_int32", [](auto cc, auto a, auto b) { return cc->CreateSDiv(a, b); }, [](int32_t a, int32_t b) { return a / b; });
		tester.Binary<int32_t>("udiv_int32", [](auto cc, auto a, auto b) { return cc->CreateUDiv(a, b); }, [](int32_t a, int32_t b) { return ((uint32_t)a) / (uint32_t)b; });
		tester.Binary<int32_t>("srem_int32", [](auto cc, auto a, auto b) { return cc->CreateSRem(a, b); }, [](int32_t a, int32_t b) { return a % b; });
		tester.Binary<int32_t>("urem_int32", [](auto cc, auto a, auto b) { return cc->CreateURem(a, b); }, [](int32_t a, int32_t b) { return ((uint32_t)a) % (uint32_t)b; });
		tester.Binary<int32_t>("and_int32", [](auto cc, auto a, auto b) { return cc->CreateAnd(a, b); }, [](int32_t a, int32_t b) { return a & b; });
		tester.Binary<int32_t>("or_int32", [](auto cc, auto a, auto b) { return cc->CreateOr(a, b); }, [](int32_t a, int32_t b) { return a | b; });
		tester.Binary<int32_t>("xor_int32", [](auto cc, auto a, auto b) { return cc->CreateXor(a, b); }, [](int32_t a, int32_t b) { return a ^ b; });
		tester.Binary<int32_t>("shl_int32", [](auto cc, auto a, auto b) { return cc->CreateShl(a, b); }, [](int32_t a, int32_t b) { return a << b; });
		tester.Binary<int32_t>("lshr_int32", [](auto cc, auto a, auto b) { return cc->CreateLShr(a, b); }, [](int32_t a, int32_t b) { return ((uint32_t)a) >> (uint32_t)b; });
		tester.Binary<int32_t>("ashr_int32", [](auto cc, auto a, auto b) { return cc->CreateAShr(a, b); }, [](int32_t a, int32_t b) { return a >> b; });

		tester.Binary<int64_t>("add_int64", [](auto cc, auto a, auto b) { return cc->CreateAdd(a, b); }, [](int64_t a, int64_t b) { return a + b; });
		tester.Binary<int64_t>("sub_int64", [](auto cc, auto a, auto b) { return cc->CreateSub(a, b); }, [](int64_t a, int64_t b) { return a - b; });
		tester.Binary<int64_t>("mul_int64", [](auto cc, auto a, auto b) { return cc->CreateMul(a, b); }, [](int64_t a, int64_t b) { return a * b; });
		tester.Binary<int64_t>("sdiv_int64", [](auto cc, auto a, auto b) { return cc->CreateSDiv(a, b); }, [](int64_t a, int64_t b) { return a / b; });
		tester.Binary<int64_t>("udiv_int64", [](auto cc, auto a, auto b) { return cc->CreateUDiv(a, b); }, [](int64_t a, int64_t b) { return ((uint64_t)a) / (uint64_t)b; });
		tester.Binary<int64_t>("srem_int64", [](auto cc, auto a, auto b) { return cc->CreateSRem(a, b); }, [](int64_t a, int64_t b) { return a % b; });
		tester.Binary<int64_t>("urem_int64", [](auto cc, auto a, auto b) { return cc->CreateURem(a, b); }, [](int64_t a, int64_t b) { return ((uint64_t)a) % (uint64_t)b; });
		tester.Binary<int64_t>("and_int64", [](auto cc, auto a, auto b) { return cc->CreateAnd(a, b); }, [](int64_t a, int64_t b) { return a & b; });
		tester.Binary<int64_t>("or_int64", [](auto cc, auto a, auto b) { return cc->CreateOr(a, b); }, [](int64_t a, int64_t b) { return a | b; });
		tester.Binary<int64_t>("xor_int64", [](auto cc, auto a, auto b) { return cc->CreateXor(a, b); }, [](int64_t a, int64_t b) { return a ^ b; });
		tester.Binary<int64_t>("shl_int64", [](auto cc, auto a, auto b) { return cc->CreateShl(a, b); }, [](int64_t a, int64_t b) { return a << b; });
		tester.Binary<int64_t>("lshr_int64", [](auto cc, auto a, auto b) { return cc->CreateLShr(a, b); }, [](int64_t a, int64_t b) { return ((uint64_t)a) >> (uint64_t)b; });
		tester.Binary<int64_t>("ashr_int64", [](auto cc, auto a, auto b) { return cc->CreateAShr(a, b); }, [](int64_t a, int64_t b) { return a >> b; });

		tester.Binary<float>("fadd_float", [](auto cc, auto a, auto b) { return cc->CreateFAdd(a, b); }, [](float a, float b) { return a + b; });
		tester.Binary<float>("fsub_float", [](auto cc, auto a, auto b) { return cc->CreateFSub(a, b); }, [](float a, float b) { return a - b; });
		tester.Binary<float>("fmul_float", [](auto cc, auto a, auto b) { return cc->CreateFMul(a, b); }, [](float a, float b) { return a * b; });
		tester.Binary<float>("fdiv_float", [](auto cc, auto a, auto b) { return cc->CreateFDiv(a, b); }, [](float a, float b) { return a / b; });

		tester.Binary<double>("fadd_double", [](auto cc, auto a, auto b) { return cc->CreateFAdd(a, b); }, [](double a, double b) { return a + b; });
		tester.Binary<double>("fsub_double", [](auto cc, auto a, auto b) { return cc->CreateFSub(a, b); }, [](double a, double b) { return a - b; });
		tester.Binary<double>("fmul_double", [](auto cc, auto a, auto b) { return cc->CreateFMul(a, b); }, [](double a, double b) { return a * b; });
		tester.Binary<double>("fdiv_double", [](auto cc, auto a, auto b) { return cc->CreateFDiv(a, b); }, [](double a, double b) { return a / b; });

		tester.Convert<int8_t, int8_t>("trunc_i8_i8", [](auto cc, auto a, auto type) { return cc->CreateTrunc(a, type); }, [](int8_t a) { return (int8_t)a; });
		tester.Convert<int8_t, int16_t>("trunc_i8_i16", [](auto cc, auto a, auto type) { return cc->CreateTrunc(a, type); }, [](int16_t a) { return (int8_t)a; });
		tester.Convert<int8_t, int32_t>("trunc_i8_i32", [](auto cc, auto a, auto type) { return cc->CreateTrunc(a, type); }, [](int32_t a) { return (int8_t)a; });
		tester.Convert<int8_t, int64_t>("trunc_i8_i64", [](auto cc, auto a, auto type) { return cc->CreateTrunc(a, type); }, [](int64_t a) { return (int8_t)a; });
		tester.Convert<int16_t, int16_t>("trunc_i16_i16", [](auto cc, auto a, auto type) { return cc->CreateTrunc(a, type); }, [](int16_t a) { return (int16_t)a; });
		tester.Convert<int16_t, int32_t>("trunc_i16_i32", [](auto cc, auto a, auto type) { return cc->CreateTrunc(a, type); }, [](int32_t a) { return (int16_t)a; });
		tester.Convert<int16_t, int64_t>("trunc_i16_i64", [](auto cc, auto a, auto type) { return cc->CreateTrunc(a, type); }, [](int64_t a) { return (int16_t)a; });
		tester.Convert<int32_t, int32_t>("trunc_i32_i32", [](auto cc, auto a, auto type) { return cc->CreateTrunc(a, type); }, [](int32_t a) { return (int32_t)a; });
		tester.Convert<int32_t, int64_t>("trunc_i32_i64", [](auto cc, auto a, auto type) { return cc->CreateTrunc(a, type); }, [](int64_t a) { return (int32_t)a; });
		tester.Convert<int64_t, int64_t>("trunc_i64_i64", [](auto cc, auto a, auto type) { return cc->CreateTrunc(a, type); }, [](int64_t a) { return (int64_t)a; });

		tester.Convert<int8_t, int8_t>("sext_i8_i8", [](auto cc, auto a, auto type) { return cc->CreateSExt(a, type); }, [](int8_t a) { return (int8_t)a; });
		tester.Convert<int16_t, int8_t>("sext_i16_i8", [](auto cc, auto a, auto type) { return cc->CreateSExt(a, type); }, [](int8_t a) { return (int16_t)a; });
		tester.Convert<int32_t, int8_t>("sext_i32_i8", [](auto cc, auto a, auto type) { return cc->CreateSExt(a, type); }, [](int8_t a) { return (int32_t)a; });
		tester.Convert<int64_t, int8_t>("sext_i64_i8", [](auto cc, auto a, auto type) { return cc->CreateSExt(a, type); }, [](int8_t a) { return (int64_t)a; });
		tester.Convert<int16_t, int16_t>("sext_i16_i16", [](auto cc, auto a, auto type) { return cc->CreateSExt(a, type); }, [](int16_t a) { return (int16_t)a; });
		tester.Convert<int32_t, int16_t>("sext_i32_i16", [](auto cc, auto a, auto type) { return cc->CreateSExt(a, type); }, [](int16_t a) { return (int32_t)a; });
		tester.Convert<int64_t, int16_t>("sext_i64_i16", [](auto cc, auto a, auto type) { return cc->CreateSExt(a, type); }, [](int16_t a) { return (int64_t)a; });
		tester.Convert<int32_t, int32_t>("sext_i32_i32", [](auto cc, auto a, auto type) { return cc->CreateSExt(a, type); }, [](int32_t a) { return (int32_t)a; });
		tester.Convert<int64_t, int32_t>("sext_i64_i32", [](auto cc, auto a, auto type) { return cc->CreateSExt(a, type); }, [](int32_t a) { return (int64_t)a; });
		tester.Convert<int64_t, int64_t>("sext_i64_i64", [](auto cc, auto a, auto type) { return cc->CreateSExt(a, type); }, [](int64_t a) { return (int64_t)a; });

		tester.Convert<int8_t, int8_t>("zext_i8_i8", [](auto cc, auto a, auto type) { return cc->CreateZExt(a, type); }, [](int8_t a) { return (int8_t)(uint8_t)a; });
		tester.Convert<int16_t, int8_t>("zext_i16_i8", [](auto cc, auto a, auto type) { return cc->CreateZExt(a, type); }, [](int8_t a) { return (int16_t)(uint8_t)a; });
		tester.Convert<int32_t, int8_t>("zext_i32_i8", [](auto cc, auto a, auto type) { return cc->CreateZExt(a, type); }, [](int8_t a) { return (int32_t)(uint8_t)a; });
		tester.Convert<int64_t, int8_t>("zext_i64_i8", [](auto cc, auto a, auto type) { return cc->CreateZExt(a, type); }, [](int8_t a) { return (int64_t)(uint8_t)a; });
		tester.Convert<int16_t, int16_t>("zext_i16_i16", [](auto cc, auto a, auto type) { return cc->CreateZExt(a, type); }, [](int16_t a) { return (int16_t)(uint16_t)a; });
		tester.Convert<int32_t, int16_t>("zext_i32_i16", [](auto cc, auto a, auto type) { return cc->CreateZExt(a, type); }, [](int16_t a) { return (int32_t)(uint16_t)a; });
		tester.Convert<int64_t, int16_t>("zext_i64_i16", [](auto cc, auto a, auto type) { return cc->CreateZExt(a, type); }, [](int16_t a) { return (int64_t)(uint16_t)a; });
		tester.Convert<int32_t, int32_t>("zext_i32_i32", [](auto cc, auto a, auto type) { return cc->CreateZExt(a, type); }, [](int32_t a) { return (int32_t)(uint32_t)a; });
		tester.Convert<int64_t, int32_t>("zext_i64_i32", [](auto cc, auto a, auto type) { return cc->CreateZExt(a, type); }, [](int32_t a) { return (int64_t)(uint32_t)a; });
		tester.Convert<int64_t, int64_t>("zext_i64_i64", [](auto cc, auto a, auto type) { return cc->CreateZExt(a, type); }, [](int64_t a) { return (int64_t)(uint64_t)a; });

		tester.Compare<int8_t>("icmpslt_i8", [](auto cc, auto a, auto b) { return cc->CreateICmpSLT(a, b); }, [](int8_t a, int8_t b) { return a < b; });
		tester.Compare<int16_t>("icmpslt_i16", [](auto cc, auto a, auto b) { return cc->CreateICmpSLT(a, b); }, [](int16_t a, int16_t b) { return a < b; });
		tester.Compare<int32_t>("icmpslt_i32", [](auto cc, auto a, auto b) { return cc->CreateICmpSLT(a, b); }, [](int32_t a, int32_t b) { return a < b; });
		tester.Compare<int64_t>("icmpslt_i64", [](auto cc, auto a, auto b) { return cc->CreateICmpSLT(a, b); }, [](int64_t a, int64_t b) { return a < b; });

		tester.Compare<int8_t>("icmpsgt_i8", [](auto cc, auto a, auto b) { return cc->CreateICmpSGT(a, b); }, [](int8_t a, int8_t b) { return a > b; });
		tester.Compare<int16_t>("icmpsgt_i16", [](auto cc, auto a, auto b) { return cc->CreateICmpSGT(a, b); }, [](int16_t a, int16_t b) { return a > b; });
		tester.Compare<int32_t>("icmpsgt_i32", [](auto cc, auto a, auto b) { return cc->CreateICmpSGT(a, b); }, [](int32_t a, int32_t b) { return a > b; });
		tester.Compare<int64_t>("icmpsgt_i64", [](auto cc, auto a, auto b) { return cc->CreateICmpSGT(a, b); }, [](int64_t a, int64_t b) { return a > b; });

		tester.Compare<int8_t>("icmpsle_i8", [](auto cc, auto a, auto b) { return cc->CreateICmpSLE(a, b); }, [](int8_t a, int8_t b) { return a <= b; });
		tester.Compare<int16_t>("icmpsle_i16", [](auto cc, auto a, auto b) { return cc->CreateICmpSLE(a, b); }, [](int16_t a, int16_t b) { return a <= b; });
		tester.Compare<int32_t>("icmpsle_i32", [](auto cc, auto a, auto b) { return cc->CreateICmpSLE(a, b); }, [](int32_t a, int32_t b) { return a <= b; });
		tester.Compare<int64_t>("icmpsle_i64", [](auto cc, auto a, auto b) { return cc->CreateICmpSLE(a, b); }, [](int64_t a, int64_t b) { return a <= b; });

		tester.Compare<int8_t>("icmpsge_i8", [](auto cc, auto a, auto b) { return cc->CreateICmpSGE(a, b); }, [](int8_t a, int8_t b) { return a >= b; });
		tester.Compare<int16_t>("icmpsge_i16", [](auto cc, auto a, auto b) { return cc->CreateICmpSGE(a, b); }, [](int16_t a, int16_t b) { return a >= b; });
		tester.Compare<int32_t>("icmpsge_i32", [](auto cc, auto a, auto b) { return cc->CreateICmpSGE(a, b); }, [](int32_t a, int32_t b) { return a >= b; });
		tester.Compare<int64_t>("icmpsge_i64", [](auto cc, auto a, auto b) { return cc->CreateICmpSGE(a, b); }, [](int64_t a, int64_t b) { return a >= b; });

		tester.Compare<uint8_t>("icmpult_i8", [](auto cc, auto a, auto b) { return cc->CreateICmpULT(a, b); }, [](uint8_t a, uint8_t b) { return a < b; });
		tester.Compare<uint16_t>("icmpult_i16", [](auto cc, auto a, auto b) { return cc->CreateICmpULT(a, b); }, [](uint16_t a, uint16_t b) { return a < b; });
		tester.Compare<uint32_t>("icmpult_i32", [](auto cc, auto a, auto b) { return cc->CreateICmpULT(a, b); }, [](uint32_t a, uint32_t b) { return a < b; });
		tester.Compare<uint64_t>("icmpult_i64", [](auto cc, auto a, auto b) { return cc->CreateICmpULT(a, b); }, [](uint64_t a, uint64_t b) { return a < b; });

		tester.Compare<uint8_t>("icmpugt_i8", [](auto cc, auto a, auto b) { return cc->CreateICmpUGT(a, b); }, [](uint8_t a, uint8_t b) { return a > b; });
		tester.Compare<uint16_t>("icmpugt_i16", [](auto cc, auto a, auto b) { return cc->CreateICmpUGT(a, b); }, [](uint16_t a, uint16_t b) { return a > b; });
		tester.Compare<uint32_t>("icmpugt_i32", [](auto cc, auto a, auto b) { return cc->CreateICmpUGT(a, b); }, [](uint32_t a, uint32_t b) { return a > b; });
		tester.Compare<uint64_t>("icmpugt_i64", [](auto cc, auto a, auto b) { return cc->CreateICmpUGT(a, b); }, [](uint64_t a, uint64_t b) { return a > b; });

		tester.Compare<uint8_t>("icmpule_i8", [](auto cc, auto a, auto b) { return cc->CreateICmpULE(a, b); }, [](uint8_t a, uint8_t b) { return a <= b; });
		tester.Compare<uint16_t>("icmpule_i16", [](auto cc, auto a, auto b) { return cc->CreateICmpULE(a, b); }, [](uint16_t a, uint16_t b) { return a <= b; });
		tester.Compare<uint32_t>("icmpule_i32", [](auto cc, auto a, auto b) { return cc->CreateICmpULE(a, b); }, [](uint32_t a, uint32_t b) { return a <= b; });
		tester.Compare<uint64_t>("icmpule_i64", [](auto cc, auto a, auto b) { return cc->CreateICmpULE(a, b); }, [](uint64_t a, uint64_t b) { return a <= b; });

		tester.Compare<uint8_t>("icmpuge_i8", [](auto cc, auto a, auto b) { return cc->CreateICmpUGE(a, b); }, [](uint8_t a, uint8_t b) { return a >= b; });
		tester.Compare<uint16_t>("icmpuge_i16", [](auto cc, auto a, auto b) { return cc->CreateICmpUGE(a, b); }, [](uint16_t a, uint16_t b) { return a >= b; });
		tester.Compare<uint32_t>("icmpuge_i32", [](auto cc, auto a, auto b) { return cc->CreateICmpUGE(a, b); }, [](uint32_t a, uint32_t b) { return a >= b; });
		tester.Compare<uint64_t>("icmpuge_i64", [](auto cc, auto a, auto b) { return cc->CreateICmpUGE(a, b); }, [](uint64_t a, uint64_t b) { return a >= b; });

		tester.Compare<uint8_t>("icmpeq_i8", [](auto cc, auto a, auto b) { return cc->CreateICmpEQ(a, b); }, [](uint8_t a, uint8_t b) { return a == b; });
		tester.Compare<uint16_t>("icmpeq_i16", [](auto cc, auto a, auto b) { return cc->CreateICmpEQ(a, b); }, [](uint16_t a, uint16_t b) { return a == b; });
		tester.Compare<uint32_t>("icmpeq_i32", [](auto cc, auto a, auto b) { return cc->CreateICmpEQ(a, b); }, [](uint32_t a, uint32_t b) { return a == b; });
		tester.Compare<uint64_t>("icmpeq_i64", [](auto cc, auto a, auto b) { return cc->CreateICmpEQ(a, b); }, [](uint64_t a, uint64_t b) { return a == b; });

		tester.Compare<uint8_t>("icmpne_i8", [](auto cc, auto a, auto b) { return cc->CreateICmpNE(a, b); }, [](uint8_t a, uint8_t b) { return a != b; });
		tester.Compare<uint16_t>("icmpne_i16", [](auto cc, auto a, auto b) { return cc->CreateICmpNE(a, b); }, [](uint16_t a, uint16_t b) { return a != b; });
		tester.Compare<uint32_t>("icmpne_i32", [](auto cc, auto a, auto b) { return cc->CreateICmpNE(a, b); }, [](uint32_t a, uint32_t b) { return a != b; });
		tester.Compare<uint64_t>("icmpne_i64", [](auto cc, auto a, auto b) { return cc->CreateICmpNE(a, b); }, [](uint64_t a, uint64_t b) { return a != b; });

		tester.Compare<float>("fcmpult_float", [](auto cc, auto a, auto b) { return cc->CreateFCmpULT(a, b); }, [](float a, float b) { return a < b; });
		tester.Compare<float>("fcmpugt_float", [](auto cc, auto a, auto b) { return cc->CreateFCmpUGT(a, b); }, [](float a, float b) { return a > b; });
		tester.Compare<float>("fcmpule_float", [](auto cc, auto a, auto b) { return cc->CreateFCmpULE(a, b); }, [](float a, float b) { return a <= b; });
		tester.Compare<float>("fcmpuge_float", [](auto cc, auto a, auto b) { return cc->CreateFCmpUGE(a, b); }, [](float a, float b) { return a >= b; });
		tester.Compare<float>("fcmpueq_float", [](auto cc, auto a, auto b) { return cc->CreateFCmpUEQ(a, b); }, [](float a, float b) { return a == b; });
		tester.Compare<float>("fcmpune_float", [](auto cc, auto a, auto b) { return cc->CreateFCmpUNE(a, b); }, [](float a, float b) { return a != b; });

		tester.Compare<double>("fcmpult_double", [](auto cc, auto a, auto b) { return cc->CreateFCmpULT(a, b); }, [](double a, double b) { return a < b; });
		tester.Compare<double>("fcmpugt_double", [](auto cc, auto a, auto b) { return cc->CreateFCmpUGT(a, b); }, [](double a, double b) { return a > b; });
		tester.Compare<double>("fcmpule_double", [](auto cc, auto a, auto b) { return cc->CreateFCmpULE(a, b); }, [](double a, double b) { return a <= b; });
		tester.Compare<double>("fcmpuge_double", [](auto cc, auto a, auto b) { return cc->CreateFCmpUGE(a, b); }, [](double a, double b) { return a >= b; });
		tester.Compare<double>("fcmpueq_double", [](auto cc, auto a, auto b) { return cc->CreateFCmpUEQ(a, b); }, [](double a, double b) { return a == b; });
		tester.Compare<double>("fcmpune_double", [](auto cc, auto a, auto b) { return cc->CreateFCmpUNE(a, b); }, [](double a, double b) { return a != b; });

		tester.NaNTest<float>("fcmpult_float_NaN", [](auto cc, auto a, auto b) { return cc->CreateFCmpULT(a, b); }, [](float a, float b) { return a < b; });
		tester.NaNTest<float>("fcmpugt_float_NaN", [](auto cc, auto a, auto b) { return cc->CreateFCmpUGT(a, b); }, [](float a, float b) { return a > b; });
		tester.NaNTest<float>("fcmpule_float_NaN", [](auto cc, auto a, auto b) { return cc->CreateFCmpULE(a, b); }, [](float a, float b) { return a <= b; });
		tester.NaNTest<float>("fcmpuge_float_NaN", [](auto cc, auto a, auto b) { return cc->CreateFCmpUGE(a, b); }, [](float a, float b) { return a >= b; });
		tester.NaNTest<float>("fcmpueq_float_NaN", [](auto cc, auto a, auto b) { return cc->CreateFCmpUEQ(a, b); }, [](float a, float b) { return a == b; });
		tester.NaNTest<float>("fcmpune_float_NaN", [](auto cc, auto a, auto b) { return cc->CreateFCmpUNE(a, b); }, [](float a, float b) { return a != b; });

		tester.NaNTest<double>("fcmpult_double_NaN", [](auto cc, auto a, auto b) { return cc->CreateFCmpULT(a, b); }, [](double a, double b) { return a < b; });
		tester.NaNTest<double>("fcmpugt_double_NaN", [](auto cc, auto a, auto b) { return cc->CreateFCmpUGT(a, b); }, [](double a, double b) { return a > b; });
		tester.NaNTest<double>("fcmpule_double_NaN", [](auto cc, auto a, auto b) { return cc->CreateFCmpULE(a, b); }, [](double a, double b) { return a <= b; });
		tester.NaNTest<double>("fcmpuge_double_NaN", [](auto cc, auto a, auto b) { return cc->CreateFCmpUGE(a, b); }, [](double a, double b) { return a >= b; });
		tester.NaNTest<double>("fcmpueq_double_NaN", [](auto cc, auto a, auto b) { return cc->CreateFCmpUEQ(a, b); }, [](double a, double b) { return a == b; });
		tester.NaNTest<double>("fcmpune_double_NaN", [](auto cc, auto a, auto b) { return cc->CreateFCmpUNE(a, b); }, [](double a, double b) { return a != b; });

		tester.Load<uint8_t>("load_i8");
		tester.Load<uint16_t>("load_i16");
		tester.Load<uint32_t>("load_i32");
		tester.Load<uint64_t>("load_i64");
		tester.Load<float>("load_float");
		tester.Load<double>("load_double");

		tester.Store<uint8_t>("store_i8");
		tester.Store<uint16_t>("store_i16");
		tester.Store<uint32_t>("store_i32");
		tester.Store<uint64_t>("store_i64");
		tester.Store<float>("store_float");
		tester.Store<double>("store_double");

		tester.FPConstants<float>("fpconstants_float");
		tester.FPConstants<double>("fpconstants_double");

		tester.RegisterSpillI<uint8_t>("registerspill_i8");
		tester.RegisterSpillI<uint16_t>("registerspill_i16");
		tester.RegisterSpillI<uint32_t>("registerspill_i32");
		tester.RegisterSpillI<uint64_t>("registerspill_i64");
		tester.RegisterSpillF<float>("registerspill_float");
		tester.RegisterSpillF<double>("registerspill_double");

		tester.Run();

		return 0;
	}
	catch (const std::exception& e)
	{
		std::cout << e.what() << std::endl;
		return 1;
	}
	catch (...)
	{
		std::cout << "Unknown exception caught!" << std::endl;
		return 1;
	}
}
