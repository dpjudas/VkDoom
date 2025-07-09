#pragma once

#include "zvulkan/vulkanobjects.h"
#include "vectors.h"
#include "vulkan/textures/vk_imagetransition.h"

class VulkanRenderDevice;
class FString;
class ShaderIncludeResult;

struct IrradianceMapPushConstants
{
	FVector3 dir;
	float padding0;
	FVector3 up;
	float padding1;
	FVector3 side;
	float padding2;
};

struct PrefilterMapPushConstants
{
	FVector3 dir;
	float roughness;
	FVector3 up;
	float padding0;
	FVector3 side;
	float padding1;
};

class VkLightprober
{
public:
	VkLightprober(VulkanRenderDevice* fb);
	~VkLightprober();

	void RenderEnvironmentMap(std::function<void(IntRect& bounds, int side)> renderFunc);
	void GenerateIrradianceMap(int probeIndex);
	void GeneratePrefilterMap(int probeIndex);
	void EndLightProbePass();

private:
	void CreateBrdfLutResources();
	void CreateEnvironmentMap();
	void CreateIrradianceMap();
	void CreatePrefilterMap();
	void GenerateBrdfLut();

	std::vector<uint32_t> CompileShader(const std::string& filename);

	static FString LoadPrivateShaderLump(const char* lumpname);
	static FString LoadPublicShaderLump(const char* lumpname);
	static ShaderIncludeResult OnInclude(FString headerName, FString includerName, size_t depth, bool system);

	struct
	{
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
		enum
		{
			textureSize = 256
		};
		std::unique_ptr<VulkanImage> cubeimage;
		std::unique_ptr<VulkanImageView> cubeview;
		std::unique_ptr<VulkanImage> zbuffer;
		std::unique_ptr<VulkanImageView> zbufferview;
		VkTextureImage renderTargets[6];
	} environmentMap;

	struct
	{
		std::unique_ptr<VulkanDescriptorSetLayout> descriptorSetLayout;
		std::unique_ptr<VulkanDescriptorPool> descriptorPool;
		std::unique_ptr<VulkanDescriptorSet> descriptorSets[6];
		std::unique_ptr<VulkanPipelineLayout> pipelineLayout;
		std::unique_ptr<VulkanPipeline> pipeline;
		std::unique_ptr<VulkanSampler> sampler;
		std::unique_ptr<VulkanImage> images[6];
		std::unique_ptr<VulkanImageView> views[6];
		std::vector<std::unique_ptr<VulkanImage>> probes;
	} irradianceMap;

	struct PrefilterMap
	{
		enum
		{
			maxlevels = 5,
			levelsSize = DFrameBuffer::prefilterMapLevelsSize
		};
		std::unique_ptr<VulkanDescriptorSetLayout> descriptorSetLayout;
		std::unique_ptr<VulkanDescriptorPool> descriptorPool;
		std::unique_ptr<VulkanDescriptorSet> descriptorSets[6 * maxlevels];
		std::unique_ptr<VulkanPipelineLayout> pipelineLayout;
		std::unique_ptr<VulkanPipeline> pipeline;
		std::unique_ptr<VulkanSampler> sampler;
		std::unique_ptr<VulkanImage> images[6 * maxlevels];
		std::unique_ptr<VulkanImageView> views[6 * maxlevels];
		std::vector<std::unique_ptr<VulkanImage>> probes;
	} prefilterMap;

	VulkanRenderDevice* fb = nullptr;
};
