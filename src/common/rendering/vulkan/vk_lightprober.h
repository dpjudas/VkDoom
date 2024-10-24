#pragma once

#include "zvulkan/vulkanobjects.h"
#include "vectors.h"

class VulkanRenderDevice;
class FString;
class ShaderIncludeResult;

struct IrradianceMapPushConstants
{
	FVector4 topLeft;
	FVector4 bottomRight;
};

struct PrefilterMapPushConstants
{
	FVector4 topLeft;
	FVector4 bottomRight;
	float roughness;
	float padding1, padding2, padding3;
};

class VkLightprober
{
public:
	VkLightprober(VulkanRenderDevice* fb);
	~VkLightprober();

private:
	void CreateBrdfLutResources();
	void CreateIrradianceMap();
	void CreatePrefilterMap();
	void GenerateBrdfLut();
	void GenerateIrradianceMap(VulkanImageView* environmentcubemap);
	void GeneratePrefilterMap(VulkanImageView* environmentcubemap);

	std::unique_ptr<VulkanShader> CompileShader(const std::string& name, const std::string& filename, const char* debugName);

	static FString LoadPrivateShaderLump(const char* lumpname);
	static FString LoadPublicShaderLump(const char* lumpname);
	static ShaderIncludeResult OnInclude(FString headerName, FString includerName, size_t depth, bool system);

	struct
	{
		std::unique_ptr<VulkanShader> shader;
		std::unique_ptr<VulkanDescriptorSetLayout> descriptorSetLayout;
		std::unique_ptr<VulkanDescriptorPool> descriptorPool;
		std::unique_ptr<VulkanDescriptorSet> descriptorSet;
		std::unique_ptr<VulkanPipelineLayout> pipelineLayout;
		std::unique_ptr<VulkanPipeline> pipeline;
		std::unique_ptr<VulkanImage> image;
		std::unique_ptr<VulkanImageView> view;
	} brdfLut;

	struct
	{
		std::unique_ptr<VulkanShader> shader;
		std::unique_ptr<VulkanDescriptorSetLayout> descriptorSetLayout;
		std::unique_ptr<VulkanDescriptorPool> descriptorPool;
		std::unique_ptr<VulkanDescriptorSet> descriptorSets[6];
		std::unique_ptr<VulkanPipelineLayout> pipelineLayout;
		std::unique_ptr<VulkanPipeline> pipeline;
		std::unique_ptr<VulkanSampler> sampler;
		std::unique_ptr<VulkanImage> images[6];
		std::unique_ptr<VulkanImageView> views[6];
	} irradianceMap;

	struct
	{
		enum
		{
			maxlevels = 5,
			levelsSize = (128 * 128 + 64 * 64 + 32 * 32 + 16 * 16 + 8 * 8) * 8
		};
		std::unique_ptr<VulkanShader> shader;
		std::unique_ptr<VulkanDescriptorSetLayout> descriptorSetLayout;
		std::unique_ptr<VulkanDescriptorPool> descriptorPool;
		std::unique_ptr<VulkanDescriptorSet> descriptorSets[6 * maxlevels];
		std::unique_ptr<VulkanPipelineLayout> pipelineLayout;
		std::unique_ptr<VulkanPipeline> pipeline;
		std::unique_ptr<VulkanSampler> sampler;
		std::unique_ptr<VulkanImage> images[6 * maxlevels];
		std::unique_ptr<VulkanImageView> views[6 * maxlevels];
	} prefilterMap;

	VulkanRenderDevice* fb = nullptr;
};
