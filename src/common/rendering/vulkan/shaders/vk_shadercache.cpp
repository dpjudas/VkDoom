
#include "vk_shadercache.h"
#include "vulkan/vk_renderdevice.h"
#include "sha1.h"
#include "filesystem.h"
#include "engineerrors.h"
#include "cmdlib.h"
#include "i_specialpaths.h"
#include <zvulkan/vulkanbuilders.h>
#include <chrono>

VkShaderCache::VkShaderCache(VulkanRenderDevice* fb) : fb(fb)
{
	FString path = M_GetCachePath(true);
	CreatePath(path.GetChars());
	CacheFilename = path + "/shadercache.zdsc";

	using namespace std::chrono;
	LaunchTime = (uint64_t)(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());

	Load();
}

VkShaderCache::~VkShaderCache()
{
	Save();
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
			VkCachedCompile& cacheinfo = it->second;

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
			{
				cacheinfo.LastUsed = LaunchTime;
				return cacheinfo.Code;
			}
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
	cachedCompile.LastUsed = LaunchTime;

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

static uint8_t readUInt8(FileReader& fr) { uint8_t v = 0; fr.Read(&v, 1); return v; }
static uint16_t readUInt16(FileReader& fr) { uint16_t v = 0; fr.Read(&v, 2); return v; }
static uint32_t readUInt32(FileReader& fr) { uint32_t v = 0; fr.Read(&v, 4); return v; }
static uint64_t readUInt64(FileReader& fr) { uint64_t v = 0; fr.Read(&v, 8); return v; }

static FString readString(FileReader& fr, std::vector<char>& strbuffer)
{
	size_t size = fr.ReadUInt32();
	if (strbuffer.size() < size + 1)
		strbuffer.resize(size + 1);
	if (fr.Read(strbuffer.data(), size) != (FileReader::Size)size)
		return {};
	strbuffer[size] = 0;
	return strbuffer.data();
}

static void writeUInt8(std::unique_ptr<FileWriter>& fw, uint8_t v) { fw->Write(&v, 1); }
static void writeUInt16(std::unique_ptr<FileWriter>& fw, uint16_t v) { fw->Write(&v, 2); }
static void writeUInt32(std::unique_ptr<FileWriter>& fw, uint32_t v) { fw->Write(&v, 4); }
static void writeUInt64(std::unique_ptr<FileWriter>& fw, uint64_t v) { fw->Write(&v, 8); }

static void writeString(std::unique_ptr<FileWriter>& fw, const FString& s)
{
	writeUInt32(fw, s.Len());
	fw->Write(s.GetChars(), s.Len());
}

void VkShaderCache::Load()
{
	try
	{
		FileReader fr;
		if (fr.OpenFile(CacheFilename.GetChars()))
		{
			char magic[11] = {};
			fr.Read(magic, 11);
			if (memcmp(magic, "shadercache", 11) != 0)
				return;

			uint32_t version = readUInt32(fr);
			if (version != 2)
				return;

			std::vector<char> strbuffer;

			uint32_t count = readUInt32(fr);
			while (count > 0)
			{
				FString checksum = readString(fr, strbuffer);

				VkCachedCompile cachedCompile;
				cachedCompile.LastUsed = readUInt64(fr);
				cachedCompile.Code.resize(readUInt32(fr));
				if (fr.Read(cachedCompile.Code.data(), cachedCompile.Code.size() * sizeof(uint32_t)) != (FileReader::Size)cachedCompile.Code.size() * sizeof(uint32_t))
					return;

				uint32_t includecount = readUInt32(fr);
				while (includecount > 0)
				{
					VkCachedInclude cachedInclude;
					cachedInclude.LumpName = readString(fr, strbuffer);
					cachedInclude.PrivateLump = readUInt8(fr) != 0;
					cachedInclude.Checksum = readString(fr, strbuffer);
					cachedCompile.Includes.push_back(std::move(cachedInclude));
					includecount--;
				}

				// Drop entry if it hasn't been used for 30 days
				if (cachedCompile.LastUsed + 30 * 24 * 60 * 60 > LaunchTime)
				{
					CodeCache[checksum] = std::move(cachedCompile);
				}
				count--;
			}
		}
	}
	catch (...)
	{
	}
}

void VkShaderCache::Save()
{
	try
	{
		std::unique_ptr<FileWriter> fw(FileWriter::Open(CacheFilename.GetChars()));
		if (!fw)
			return;

		fw->Write("shadercache", 11);

		uint32_t version = 2;
		writeUInt32(fw, version);

		writeUInt32(fw, CodeCache.size());
		for (const auto& it : CodeCache)
		{
			const FString& checksum = it.first;
			const VkCachedCompile& cachedCompile = it.second;

			writeString(fw, it.first);
			writeUInt64(fw, cachedCompile.LastUsed);
			writeUInt32(fw, (uint32_t)cachedCompile.Code.size());
			fw->Write(cachedCompile.Code.data(), cachedCompile.Code.size() * sizeof(uint32_t));

			writeUInt32(fw, cachedCompile.Includes.size());
			for (const VkCachedInclude& cachedInclude : cachedCompile.Includes)
			{
				writeString(fw, cachedInclude.LumpName);
				writeUInt8(fw, cachedInclude.PrivateLump ? 1 : 0);
				writeString(fw, cachedInclude.Checksum);
			}
		}
	}
	catch (...)
	{
	}
}

/////////////////////////////////////////////////////////////////////////////

std::vector<uint32_t> CachedGLSLCompiler::Compile(VulkanRenderDevice* fb)
{
	return fb->GetShaderCache()->Compile(shaderType, TArrayView<VkShaderSource>(sources.Data(), sources.Size()), includeFilter);
}
