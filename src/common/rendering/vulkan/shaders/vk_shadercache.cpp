
#include "vk_shadercache.h"
#include "vulkan/vk_renderdevice.h"
#include "sha1.h"
#include "filesystem.h"
#include "engineerrors.h"
#include "cmdlib.h"
#include <zvulkan/vulkanbuilders.h>

VkShaderCache::VkShaderCache(VulkanRenderDevice* fb) : fb(fb)
{
}

std::vector<uint32_t> VkShaderCache::Compile(ShaderType type, const TArrayView<VkShaderSource>& sources, const std::function<FString(FString)>& includeFilter)
{
	// Is this something we already compiled before?

	FString checksum = CalcSha1(type, sources);
	auto it = CodeCache.find(checksum);
	if (it != CodeCache.end())
	{
		// OK we found a match. Did any of the include files change?

		try
		{
			const VkCachedCompile& cacheinfo = it->second;

			bool foundChanges = false;
			for (const VkCachedInclude& includeinfo : cacheinfo.Includes)
			{
				const VkCachedShaderLump& lumpinfo = includeinfo.PrivateLump ? GetPrivateFile(includeinfo.LumpName) : GetPublicFile(includeinfo.LumpName);
				if (lumpinfo.Checksum != includeinfo.Checksum)
				{
					foundChanges = true;
					break;
				}
			}

			if (!foundChanges)
				return cacheinfo.Code;
		}
		catch (...)
		{
			// If GetPrivateFile or GetPublicFile is unable to find the file anymore it must be out of date.
		}
	}

	// No match or out of date
	// Compile it and store the dependencies:

	auto noFilter = [](FString s) { return s; };

	VkCachedCompile cachedCompile;
	GLSLCompiler compiler;
	compiler.Type(type);
	compiler.OnIncludeLocal([&](std::string headerName, std::string includerName, size_t depth) { return OnInclude(cachedCompile, headerName.c_str(), includerName.c_str(), depth, false, includeFilter ? includeFilter : noFilter); });
	compiler.OnIncludeSystem([&](std::string headerName, std::string includerName, size_t depth) { return OnInclude(cachedCompile, headerName.c_str(), includerName.c_str(), depth, true, includeFilter ? includeFilter : noFilter); });
	for (const VkShaderSource& source : sources)
		compiler.AddSource(source.Name, source.Code);
	cachedCompile.Code = compiler.Compile(fb->GetDevice());

	auto& c = CodeCache[checksum];
	c = std::move(cachedCompile);
	return c.Code;
}

ShaderIncludeResult VkShaderCache::OnInclude(VkCachedCompile& cachedCompile, FString headerName, FString includerName, size_t depth, bool system, const std::function<FString(FString)>& includeFilter)
{
	if (depth > 8)
		I_Error("Too much include recursion!");

	const VkCachedShaderLump& file = system ? GetPrivateFile(headerName) : GetPublicFile(headerName);

	VkCachedInclude cacheinfo;
	cacheinfo.LumpName = headerName;
	cacheinfo.PrivateLump = system;
	cacheinfo.Checksum = file.Checksum;
	cachedCompile.Includes.push_back(std::move(cacheinfo));

	FString includeguardname;
	includeguardname << "_HEADERGUARD_" << headerName.GetChars();
	includeguardname.ReplaceChars("/\\.", '_');

	FString code;
	code << "#ifndef " << includeguardname.GetChars() << "\n";
	code << "#define " << includeguardname.GetChars() << "\n";
	code << "#line 1\n";
	code << includeFilter(file.Code.GetChars()) << "\n";
	code << "#endif\n";

	return ShaderIncludeResult(headerName.GetChars(), code.GetChars());
}

FString VkShaderCache::CalcSha1(ShaderType type, const TArrayView<VkShaderSource>& sources)
{
	size_t totalSize = 0;
	SHA1 sha1;
	for (const VkShaderSource& source : sources)
	{
		totalSize = source.Code.size();
		sha1.update(source.Code);
	}
	return std::to_string((int)type) + "-" + sha1.final() + "-" + std::to_string(totalSize);
}

FString VkShaderCache::CalcSha1(const FString& str)
{
	SHA1 sha1;
	sha1.update(str.GetChars());
	return sha1.final();
}

const VkCachedShaderLump& VkShaderCache::GetPublicFile(const FString& lumpname)
{
	auto it = PublicFiles.find(lumpname);
	if (it != PublicFiles.end())
		return it->second;

	VkCachedShaderLump cacheinfo;
	cacheinfo.Code = LoadPublicShaderLump(lumpname.GetChars());
	cacheinfo.Checksum = CalcSha1(cacheinfo.Code);

	auto& result = PublicFiles[lumpname];
	result = std::move(cacheinfo);
	return result;
}

const VkCachedShaderLump& VkShaderCache::GetPrivateFile(const FString& lumpname)
{
	auto it = PrivateFiles.find(lumpname);
	if (it != PrivateFiles.end())
		return it->second;

	VkCachedShaderLump cacheinfo;
	cacheinfo.Code = LoadPrivateShaderLump(lumpname.GetChars());
	cacheinfo.Checksum = CalcSha1(cacheinfo.Code);

	auto& result = PrivateFiles[lumpname];
	result = std::move(cacheinfo);
	return result;
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

/////////////////////////////////////////////////////////////////////////////

std::vector<uint32_t> CachedGLSLCompiler::Compile(VulkanRenderDevice* fb)
{
	return fb->GetShaderCache()->Compile(shaderType, TArrayView<VkShaderSource>(sources.Data(), sources.Size()), includeFilter);
}
