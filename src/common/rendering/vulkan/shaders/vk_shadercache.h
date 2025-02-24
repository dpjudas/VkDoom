#pragma once

#include "common/utility/zstring.h"
#include <memory>
#include <map>

class VulkanRenderDevice;

class VkShaderSourceFile
{
public:
	FString Sha1;
	FString Sourcecode;
};

class VkShaderCache
{
public:
	VkShaderCache(VulkanRenderDevice* fb);

private:
	const VkShaderSourceFile& GetPublicFile(const FString& lumpname);
	const VkShaderSourceFile& GetPrivateFile(const FString& lumpname);

	static FString LoadPublicShaderLump(const char* lumpname);
	static FString LoadPrivateShaderLump(const char* lumpname);
	static FString CalcSha1(const FString& str);

	VulkanRenderDevice* fb;

	std::map<FString, VkShaderSourceFile> PublicFiles;
	std::map<FString, VkShaderSourceFile> PrivateFiles;
};
