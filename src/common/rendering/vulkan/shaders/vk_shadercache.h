#pragma once

#include "common/utility/zstring.h"
#include "common/utility/templates.h"
#include <memory>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <zvulkan/vulkanbuilders.h>

class VulkanRenderDevice;
enum class ShaderType;
class ShaderIncludeResult;

class VkShaderSource
{
public:
	std::string Name;
	std::string Code;
};

class VkCachedShaderLump
{
public:
	FString Checksum;
	FString Code;
};

class VkCachedInclude
{
public:
	FString LumpName;
	bool PrivateLump = false;
	FString Checksum;
};

class VkCachedCompile
{
public:
	uint64_t LastUsed = 0;
	std::vector<uint32_t> Code;
	std::vector<VkCachedInclude> Includes;
};

class VkShaderCache
{
public:
	VkShaderCache(VulkanRenderDevice* fb);
	~VkShaderCache();

	std::vector<uint32_t> Compile(ShaderType type, const TArrayView<VkShaderSource>& sources, const std::function<FString(FString)>& includeFilter = {});

	FString GetPublicFileText(const FString& lumpname);
	FString GetPrivateFileText(const FString& lumpname);

private:
	void Load();
	void Save();

	std::vector<uint32_t> GetFromCache(const FString& key);
	std::vector<uint32_t> AddToCache(const FString& key, VkCachedCompile cachedCompile);

	FString CalcSha1(ShaderType type, const TArrayView<VkShaderSource>& sources);
	FString CalcSha1(const FString& str);

	ShaderIncludeResult OnInclude(VkCachedCompile& cachedCompile, FString headerName, FString includerName, size_t depth, bool system, const std::function<FString(FString)>& includeFilter);

	const VkCachedShaderLump& GetPublicFile(const FString& lumpname);
	const VkCachedShaderLump& GetPrivateFile(const FString& lumpname);

	static FString LoadPublicShaderLump(const char* lumpname);
	static FString LoadPrivateShaderLump(const char* lumpname);

	FString CacheFilename;
	uint64_t LaunchTime = 0;

	VulkanRenderDevice* fb = nullptr;

	std::mutex Mutex;
	std::map<FString, VkCachedShaderLump> PublicFiles;
	std::map<FString, VkCachedShaderLump> PrivateFiles;
	std::map<FString, VkCachedCompile> CodeCache;
};

class CachedGLSLCompiler
{
public:
	CachedGLSLCompiler& Type(ShaderType type)
	{
		shaderType = type;
		return *this;
	}

	CachedGLSLCompiler& AddSource(std::string name, std::string code)
	{
		VkShaderSource source;
		source.Name = std::move(name);
		source.Code = std::move(code);
		sources.Push(std::move(source));
		return *this;
	}

	CachedGLSLCompiler& IncludeFilter(std::function<FString(FString)> filter)
	{
		includeFilter = std::move(filter);
		return *this;
	}

	std::vector<uint32_t> Compile(VulkanRenderDevice* fb);

private:
	ShaderType shaderType = {};
	TArray<VkShaderSource> sources;
	std::function<FString(FString)> includeFilter;
};
