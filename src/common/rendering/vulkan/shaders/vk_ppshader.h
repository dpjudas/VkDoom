
#pragma once

#include "hwrenderer/postprocessing/hw_postprocess.h"
#include <zvulkan/vulkanobjects.h>
#include <list>

class VulkanRenderDevice;
class ShaderIncludeResult;

class VkPPShader : public PPShaderBackend
{
public:
	VkPPShader(VulkanRenderDevice* fb, PPShader *shader);
	~VkPPShader();

	void Reset();

	VulkanRenderDevice* fb = nullptr;
	std::list<VkPPShader*>::iterator it;

	std::unique_ptr<VulkanShader> VertexShader;
	std::unique_ptr<VulkanShader> FragmentShader;

private:
	FString LoadShaderCode(const FString &lumpname, const FString &defines, int version);
	static FString CreateUniformBlockDecl(const char* name, const std::vector<UniformFieldDesc>& fields, int bindingpoint);

	ShaderIncludeResult OnInclude(FString headerName, FString includerName, size_t depth, bool system);
	FString LoadPublicShaderLump(const char* lumpname);
	FString LoadPrivateShaderLump(const char* lumpname);
};
