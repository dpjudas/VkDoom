#pragma once

#include <type_traits>
#include <vector>
#include <map>
#include <cstddef>

#include "tarray.h"
#include "zstring.h"

enum class UniformType
{
	Undefined,
	Int,
	UInt,
	Float,
	Vec2,
	Vec3,
	Vec4,
	IVec2,
	IVec3,
	IVec4,
	UVec2,
	UVec3,
	UVec4,
	Mat4
};


const char* GetTypeStr(UniformType type);

struct UserUniformValue
{
	UniformType Type = UniformType::Undefined;
	double Values[4] = { 0.0, 0.0, 0.0, 0.0 };
};

struct UniformFieldDesc
{
	FString Name;
	UniformType Type;
	std::size_t Offset;
};

struct VaryingFieldDesc
{
	FString Name;
	FString Property;
	UniformType Type;
};

struct UniformField
{
	UniformType Type = UniformType::Undefined;
	void * Value = nullptr;
};

class UniformStructHolder
{
public:
	UniformStructHolder()
	{
	}

	UniformStructHolder(const UniformStructHolder &src)
	{
		*this = src;
	}

	~UniformStructHolder()
	{
		Clear();
	}

	UniformStructHolder &operator=(const UniformStructHolder &src)
	{
		Clear();

		if(src.alloc)
		{
			alloc = true;
			sz = src.sz;
			addr = new uint8_t[sz];
			memcpy((uint8_t*)addr, src.addr, sz);
		}
		else
		{
			sz = src.sz;
			addr = src.addr;
		}

		return *this;
	}

	void Clear()
	{
		if(alloc)
		{
			delete[] addr;
		}
		alloc = false;
		addr = nullptr;
		sz = 0;
	}

	template<typename T>
	void Set(const T *v)
	{
		Clear();

		sz = sizeof(T);
		addr = reinterpret_cast<const uint8_t*>(v);
	}

	template<typename T>
	void Set(const T &v, typename std::enable_if<!std::is_pointer_v<T>, bool>::type enabled = true)
	{
		static_assert(std::is_trivially_copyable_v<T>);

		alloc = true;
		sz = sizeof(T);
		addr = new uint8_t[sizeof(T)];
		memcpy((uint8_t*)addr, &v, sizeof(T));
	}

	bool alloc = false;
	size_t sz = 0;
	const uint8_t * addr = nullptr;
};

class UserUniforms
{
	void AddUniformField(size_t &offset, const FString &name, UniformType type, size_t fieldsize, size_t alignment = 0);
	void BuildStruct(const TMap<FString, UserUniformValue> &Uniforms);
public:
	UserUniforms() = default;
	UserUniforms(const TMap<FString, UserUniformValue> &Uniforms)
	{
		LoadUniforms(Uniforms);
	}

	~UserUniforms()
	{
		if(UniformStruct) delete[] UniformStruct;
	}

	//must only be called once
	void LoadUniforms(const TMap<FString, UserUniformValue> &Uniforms);
	void WriteUniforms(UniformStructHolder &Data) const;


	int UniformStructSize = 0;
	uint8_t * UniformStruct = nullptr;

	UniformField GetField(const FString &name);

	std::vector<UniformFieldDesc> Fields;
	TMap<FString, size_t> FieldNames;
};