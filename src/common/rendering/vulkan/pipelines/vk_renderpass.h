
#pragma once

#include "zvulkan/vulkanobjects.h"
#include "renderstyle.h"
#include "hwrenderer/data/buffers.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"
#include "hw_renderstate.h"
#include "common/rendering/vulkan/shaders/vk_shader.h"
#include <string.h>
#include <map>

class VulkanRenderDevice;
class ColorBlendAttachmentBuilder;
class VkPPShader;
class VkPPRenderPassKey;
class VkPPRenderPassSetup;
class ColorBlendAttachmentBuilder;

class VkPipelineKey
{
public:
	union
	{
		struct
		{
			uint64_t DrawType : 3;
			uint64_t CullMode : 2;
			uint64_t ColorMask : 4;
			uint64_t DepthWrite : 1;
			uint64_t DepthTest : 1;
			uint64_t DepthClamp : 1;
			uint64_t DepthBias : 1;
			uint64_t DepthFunc : 2;
			uint64_t StencilTest : 1;
			uint64_t StencilPassOp : 2;
			uint64_t Unused : 46;
		};
		uint64_t AsQWORD = 0;
	};

	int VertexFormat = 0;
	int Padding0 = 0;

	VkShaderKey ShaderKey;
	FRenderStyle RenderStyle;

	int Padding1 = 0; // for 64 bit alignment

	bool operator<(const VkPipelineKey &other) const { return memcmp(this, &other, sizeof(VkPipelineKey)) < 0; }
	bool operator==(const VkPipelineKey &other) const { return memcmp(this, &other, sizeof(VkPipelineKey)) == 0; }
	bool operator!=(const VkPipelineKey &other) const { return memcmp(this, &other, sizeof(VkPipelineKey)) != 0; }
};

static_assert(sizeof(FRenderStyle) == 4, "sizeof(FRenderStyle) is not its expected size!");
static_assert(sizeof(VkShaderKey) == 16, "sizeof(VkShaderKey) is not its expected size!");
static_assert(sizeof(VkPipelineKey) == 16 + 16 + 8, "sizeof(VkPipelineKey) is not its expected size!"); // If this assert fails, the flags union no longer adds up to 64 bits. Or there are gaps in the class so the memcmp doesn't work.

class VkRenderPassKey
{
public:
	int DepthStencil = 0;
	int Samples = 0;
	int DrawBuffers = 0;
	VkFormat DrawBufferFormat = VK_FORMAT_UNDEFINED;

	bool operator<(const VkRenderPassKey &other) const { return memcmp(this, &other, sizeof(VkRenderPassKey)) < 0; }
	bool operator==(const VkRenderPassKey &other) const { return memcmp(this, &other, sizeof(VkRenderPassKey)) == 0; }
	bool operator!=(const VkRenderPassKey &other) const { return memcmp(this, &other, sizeof(VkRenderPassKey)) != 0; }
};

struct PipelineData
{
	std::unique_ptr<VulkanPipeline> pipeline;
	UniformStructHolder Uniforms;
};

class VkRenderPassSetup
{
public:
	VkRenderPassSetup(VulkanRenderDevice* fb, const VkRenderPassKey &key);

	VulkanRenderPass *GetRenderPass(int clearTargets);
	VulkanPipeline *GetPipeline(const VkPipelineKey &key, UniformStructHolder &Uniforms);

	VkRenderPassKey PassKey;
	std::unique_ptr<VulkanRenderPass> RenderPasses[8];
	std::map<VkPipelineKey, PipelineData> Pipelines;

private:
	std::unique_ptr<VulkanRenderPass> CreateRenderPass(int clearTargets);
	std::unique_ptr<VulkanPipeline> CreatePipeline(const VkPipelineKey &key, UniformStructHolder &Uniforms);

	VulkanRenderDevice* fb = nullptr;
};

class VkVertexFormat
{
public:
	std::vector<size_t> BufferStrides;
	std::vector<FVertexBufferAttribute> Attrs;
	int UseVertexData;
};

ColorBlendAttachmentBuilder& BlendMode(ColorBlendAttachmentBuilder& builder, const FRenderStyle& style);

class VkRenderPassManager
{
public:
	VkRenderPassManager(VulkanRenderDevice* fb);
	~VkRenderPassManager();

	void RenderBuffersReset();

	VkRenderPassSetup *GetRenderPass(const VkRenderPassKey &key);
	int GetVertexFormat(const std::vector<size_t>& bufferStrides, const std::vector<FVertexBufferAttribute>& attrs);
	VkVertexFormat *GetVertexFormat(int index);
	VulkanPipelineLayout* GetPipelineLayout(bool levelmesh, int UserUniformSize);

	VkPPRenderPassSetup* GetPPRenderPass(const VkPPRenderPassKey& key);

	VulkanPipelineCache* GetCache() { return PipelineCache.get(); }

	VulkanPipelineLayout* GetLightTilesLayout() { return LightTiles.Layout.get(); }
	VulkanPipeline* GetLightTilesPipeline() { return LightTiles.Pipeline.get(); }

	VulkanRenderPass* GetZMinMaxRenderPass() { return ZMinMax.RenderPass.get(); }
	VulkanPipelineLayout* GetZMinMaxLayout() { return ZMinMax.Layout.get(); }
	VulkanPipeline* GetZMinMaxPipeline0(VkSampleCountFlagBits samples) { return ZMinMax.Pipeline[samples > 1 ? 1 : 0].get(); }
	VulkanPipeline* GetZMinMaxPipeline1() { return ZMinMax.Pipeline[2].get(); }

private:
	void CreateLightTilesPipeline();
	void CreateZMinMaxPipeline();

	VulkanRenderDevice* fb = nullptr;

	std::map<VkRenderPassKey, std::unique_ptr<VkRenderPassSetup>> RenderPassSetup;
	std::unique_ptr<VulkanPipelineLayout> PipelineLayouts[2];
	std::vector<VkVertexFormat> VertexFormats;

	std::map<VkPPRenderPassKey, std::unique_ptr<VkPPRenderPassSetup>> PPRenderPassSetup;

	struct
	{
		std::unique_ptr<VulkanPipelineLayout> Layout;
		std::unique_ptr<VulkanPipeline> Pipeline;
	} LightTiles;

	struct
	{
		std::unique_ptr<VulkanRenderPass> RenderPass;
		std::unique_ptr<VulkanPipelineLayout> Layout;
		std::unique_ptr<VulkanPipeline> Pipeline[3];
	} ZMinMax;

	FString CacheFilename;
	std::unique_ptr<VulkanPipelineCache> PipelineCache;
};
