
#include "vk_shadercache.h"
#include "sha1.h"
#include "filesystem.h"
#include "engineerrors.h"
#include "cmdlib.h"
#include <zvulkan/vulkanbuilders.h>

VkShaderCache::VkShaderCache(VulkanRenderDevice* fb) : fb(fb)
{
}

const VkShaderSourceFile& VkShaderCache::GetPublicFile(const FString& lumpname)
{
	auto it = PublicFiles.find(lumpname);
	if (it != PublicFiles.end())
		return it->second;

	FString code = LoadPublicShaderLump(lumpname.GetChars());
	FString sha1 = CalcSha1(code);

	VkShaderSourceFile& file = PublicFiles[lumpname];
	file.Sourcecode = std::move(code);
	file.Sha1 = std::move(sha1);
	return file;
}

const VkShaderSourceFile& VkShaderCache::GetPrivateFile(const FString& lumpname)
{
	auto it = PrivateFiles.find(lumpname);
	if (it != PrivateFiles.end())
		return it->second;

	FString code = LoadPrivateShaderLump(lumpname.GetChars());
	FString sha1 = CalcSha1(code);

	VkShaderSourceFile& file = PrivateFiles[lumpname];
	file.Sourcecode = std::move(code);
	file.Sha1 = std::move(sha1);
	return file;
}

FString VkShaderCache::LoadPublicShaderLump(const char* lumpname)
{
	int lump = fileSystem.CheckNumForFullName(lumpname, 0);
	if (lump == -1) lump = fileSystem.CheckNumForFullName(lumpname);
	if (lump == -1) I_Error("Unable to load '%s'", lumpname);
	return GetStringFromLump(lump);
}

FString VkShaderCache::LoadPrivateShaderLump(const char* lumpname)
{
	int lump = fileSystem.CheckNumForFullName(lumpname, 0);
	if (lump == -1) I_Error("Unable to load '%s'", lumpname);
	return GetStringFromLump(lump);
}

FString VkShaderCache::CalcSha1(const FString& str)
{
	SHA1 sha1;
	sha1.update(str.GetChars());
	return sha1.final();
}
