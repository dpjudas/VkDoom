
#pragma once

#include "zvulkan/vulkanobjects.h"
#include "vulkan/textures/vk_imagetransition.h"

class VulkanRenderDevice;
class VkPPRenderPassSetup;
class PPOutput;

enum class WhichDepthStencil {
	None,
	Scene,
	Pipeline,
};

class VkRenderBuffers
{
public:
	VkRenderBuffers(VulkanRenderDevice* fb);
	~VkRenderBuffers();

	void BeginFrame(int width, int height, int sceneWidth, int sceneHeight);

	int GetWidth() const { return mWidth; }
	int GetHeight() const { return mHeight; }
	int GetSceneWidth() const { return mSceneWidth; }
	int GetSceneHeight() const { return mSceneHeight; }
	VkSampleCountFlagBits GetSceneSamples() const { return mSamples; }

	VulkanFramebuffer* GetFramebuffer(const VkRenderPassKey& key);
	VulkanFramebuffer* GetZMinMaxFramebuffer(int index);

	VkTextureImage SceneColor;
	VkTextureImage SceneDepthStencil;
	VkTextureImage SceneNormal;
	VkTextureImage SceneFog;
	VkTextureImage SceneLinearDepth;

	VkTextureImage SceneZMinMax[6];
	std::unique_ptr<VulkanBuffer> SceneLightTiles;

	static const int NumPipelineImages = 2;
	VkTextureImage PipelineDepthStencil;
	VkTextureImage PipelineImage[NumPipelineImages];

	VulkanFramebuffer* GetOutput(VkPPRenderPassSetup* passSetup, const PPOutput& output, WhichDepthStencil stencilTest, int& framebufferWidth, int& framebufferHeight);

private:
	void CreatePipelineDepthStencil(int width, int height);
	void CreatePipeline(int width, int height);
	void CreateScene(int width, int height, VkSampleCountFlagBits samples);
	void CreateSceneColor(int width, int height, VkSampleCountFlagBits samples);
	void CreateSceneDepthStencil(int width, int height, VkSampleCountFlagBits samples);
	void CreateSceneFog(int width, int height, VkSampleCountFlagBits samples);
	void CreateSceneNormal(int width, int height, VkSampleCountFlagBits samples);
	void CreateSceneLinearDepth(int width, int height);
	void CreateSceneZMinMax(int width, int height);
	void CreateSceneLightTiles(int width, int height);
	VkSampleCountFlagBits GetBestSampleCount();

	VulkanRenderDevice* fb = nullptr;

	int mWidth = 0;
	int mHeight = 0;
	int mSceneWidth = 0;
	int mSceneHeight = 0;
	VkSampleCountFlagBits mSamples = VK_SAMPLE_COUNT_1_BIT;
};
