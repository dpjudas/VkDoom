/*
**  Vulkan backend
**  Copyright (c) 2016-2020 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
*/

#include "vk_renderpass.h"
#include "vk_pprenderpass.h"
#include "vulkan/vk_renderstate.h"
#include "vulkan/vk_renderdevice.h"
#include "vulkan/vk_levelmesh.h"
#include "vulkan/descriptorsets/vk_descriptorset.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include "vulkan/samplers/vk_samplers.h"
#include "vulkan/shaders/vk_shader.h"
#include "vulkan/buffers/vk_hwbuffer.h"
#include <zvulkan/vulkanbuilders.h>
#include "flatvertices.h"
#include "hw_viewpointuniforms.h"
#include "v_2ddrawer.h"
#include "i_specialpaths.h"
#include "cmdlib.h"

VkRenderPassManager::VkRenderPassManager(VulkanRenderDevice* fb) : fb(fb)
{
	FString path = M_GetCachePath(true);
	CreatePath(path.GetChars());
	CacheFilename = path + "/pipelinecache.zdpc";

	PipelineCacheBuilder builder;
	builder.DebugName("PipelineCache");

	try
	{
		FileReader fr;
		if (fr.OpenFile(CacheFilename.GetChars()))
		{
			std::vector<uint8_t> data;
			data.resize(fr.GetLength());
			if (fr.Read(data.data(), data.size()) == (FileReader::Size)data.size())
			{
				builder.InitialData(data.data(), data.size());
			}
		}
	}
	catch (...)
	{
	}

	PipelineCache = builder.Create(fb->GetDevice());

	CreateLightTilesPipeline();
	CreateZMinMaxPipeline();
}

VkRenderPassManager::~VkRenderPassManager()
{
	try
	{
		auto data = PipelineCache->GetCacheData();
		std::unique_ptr<FileWriter> fw(FileWriter::Open(CacheFilename.GetChars()));
		if (fw)
			fw->Write(data.data(), data.size());
	}
	catch (...)
	{
	}
}

void VkRenderPassManager::RenderBuffersReset()
{
	RenderPassSetup.clear();
	PPRenderPassSetup.clear();
}

VkRenderPassSetup *VkRenderPassManager::GetRenderPass(const VkRenderPassKey &key)
{
	auto &item = RenderPassSetup[key];
	if (!item)
		item.reset(new VkRenderPassSetup(fb, key));
	return item.get();
}

int VkRenderPassManager::GetVertexFormat(const std::vector<size_t>& bufferStrides, const std::vector<FVertexBufferAttribute>& attrs)
{
	for (size_t i = 0; i < VertexFormats.size(); i++)
	{
		const auto &f = VertexFormats[i];
		if (f.BufferStrides.size() == bufferStrides.size() &&
			f.Attrs.size() == attrs.size() &&
			memcmp(f.BufferStrides.data(), bufferStrides.data(), bufferStrides.size() * sizeof(size_t)) == 0 &&
			memcmp(f.Attrs.data(), attrs.data(), attrs.size() * sizeof(FVertexBufferAttribute)) == 0)
		{
			return (int)i;
		}
	}

	VkVertexFormat fmt;
	fmt.BufferStrides = bufferStrides;
	fmt.Attrs = attrs;
	fmt.UseVertexData = 0;
	for (const FVertexBufferAttribute& attr : fmt.Attrs)
	{
		if (attr.location == VATTR_COLOR)
			fmt.UseVertexData |= 1;
		else if (attr.location == VATTR_NORMAL)
			fmt.UseVertexData |= 2;
	}
	VertexFormats.push_back(fmt);
	return (int)VertexFormats.size() - 1;
}

VkVertexFormat *VkRenderPassManager::GetVertexFormat(int index)
{
	return &VertexFormats[index];
}

VulkanPipelineLayout* VkRenderPassManager::GetPipelineLayout(bool levelmesh, int UserUniformSize)
{
	auto &layout = PipelineLayouts[levelmesh];
	if (layout)
		return layout.get();

	auto descriptors = fb->GetDescriptorSetManager();

	PipelineLayoutBuilder builder;
	builder.AddSetLayout(descriptors->GetFixedLayout());
	builder.AddSetLayout(levelmesh ? descriptors->GetLevelMeshLayout() : descriptors->GetRSBufferLayout());
	builder.AddSetLayout(descriptors->GetBindlessLayout());
	builder.AddPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants));

	if(UserUniformSize > 0)
	{
		builder.AddPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushConstants), UserUniformSize);
	}

	builder.DebugName("VkRenderPassManager.PipelineLayout");
	layout = builder.Create(fb->GetDevice());
	return layout.get();
}

VkPPRenderPassSetup* VkRenderPassManager::GetPPRenderPass(const VkPPRenderPassKey& key)
{
	auto& passSetup = PPRenderPassSetup[key];
	if (!passSetup)
		passSetup.reset(new VkPPRenderPassSetup(fb, key));
	return passSetup.get();
}

void VkRenderPassManager::CreateLightTilesPipeline()
{
	LightTiles.Layout = PipelineLayoutBuilder()
		.AddSetLayout(fb->GetDescriptorSetManager()->GetLightTilesLayout())
		.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(LightTilesPushConstants))
		.DebugName("VkRenderPassManager.LightTiles.Layout")
		.Create(fb->GetDevice());

	LightTiles.Pipeline = ComputePipelineBuilder()
		.Cache(PipelineCache.get())
		.Layout(LightTiles.Layout.get())
		.ComputeShader(fb->GetShaderManager()->GetLightTilesShader())
		.DebugName("VkRenderPassManager.LightTiles.Pipeline")
		.Create(fb->GetDevice());
}

void VkRenderPassManager::CreateZMinMaxPipeline()
{
	ZMinMax.RenderPass = RenderPassBuilder()
		.AddAttachment(VK_FORMAT_R32G32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		.AddExternalSubpassDependency(
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
		.AddSubpass()
		.AddSubpassColorAttachmentRef(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		.DebugName("VkRenderPassManager.ZMinMax.RenderPass")
		.Create(fb->GetDevice());

	ZMinMax.Layout = PipelineLayoutBuilder()
		.AddSetLayout(fb->GetDescriptorSetManager()->GetZMinMaxLayout())
		.AddPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ZMinMaxPushConstants))
		.DebugName("VkRenderPassManager.ZMinMax.Layout")
		.Create(fb->GetDevice());

	for (int i = 0; i < 3; i++)
	{
		ZMinMax.Pipeline[i] = GraphicsPipelineBuilder()
			.Cache(PipelineCache.get())
			.RenderPass(ZMinMax.RenderPass.get())
			.Layout(ZMinMax.Layout.get())
			.AddVertexShader(fb->GetShaderManager()->GetZMinMaxVertexShader())
			.AddFragmentShader(fb->GetShaderManager()->GetZMinMaxFragmentShader(i))
			.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
			.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
			.DebugName("VkRenderPassManager.ZMinMax.Pipeline")
			.Create(fb->GetDevice());
	}
}

/////////////////////////////////////////////////////////////////////////////

VkRenderPassSetup::VkRenderPassSetup(VulkanRenderDevice* fb, const VkRenderPassKey &key) : PassKey(key), fb(fb)
{
}

std::unique_ptr<VulkanRenderPass> VkRenderPassSetup::CreateRenderPass(int clearTargets)
{
	auto buffers = fb->GetBuffers();

	VkFormat drawBufferFormats[] = { VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, buffers->SceneNormalFormat };

	RenderPassBuilder builder;

	builder.AddAttachment(
		PassKey.DrawBufferFormat, (VkSampleCountFlagBits)PassKey.Samples,
		(clearTargets & CT_Color) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	for (int i = 1; i < PassKey.DrawBuffers; i++)
	{
		builder.AddAttachment(
			drawBufferFormats[i], (VkSampleCountFlagBits)PassKey.Samples,
			(clearTargets & CT_Color) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}
	if (PassKey.DepthStencil)
	{
		builder.AddDepthStencilAttachment(
			buffers->SceneDepthStencilFormat, (VkSampleCountFlagBits)PassKey.Samples,
			(clearTargets & CT_Depth) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			(clearTargets & CT_Stencil) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}
	builder.AddSubpass();
	for (int i = 0; i < PassKey.DrawBuffers; i++)
		builder.AddSubpassColorAttachmentRef(i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	if (PassKey.DepthStencil)
	{
		builder.AddSubpassDepthStencilAttachmentRef(PassKey.DrawBuffers, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		builder.AddExternalSubpassDependency(
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
	}
	else
	{
		builder.AddExternalSubpassDependency(
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
	}
	builder.DebugName("VkRenderPassSetup.RenderPass");
	return builder.Create(fb->GetDevice());
}

VulkanRenderPass *VkRenderPassSetup::GetRenderPass(int clearTargets)
{
	if (!RenderPasses[clearTargets])
		RenderPasses[clearTargets] = CreateRenderPass(clearTargets);
	return RenderPasses[clearTargets].get();
}

VulkanPipeline *VkRenderPassSetup::GetPipeline(const VkPipelineKey &key, UniformStructHolder &Uniforms)
{
	auto item = Pipelines.find(key);
	if (item == Pipelines.end())
	{
		Uniforms.Clear();
		auto pipeline = CreatePipeline(key, Uniforms);
		auto ptr = pipeline.get();
		Pipelines.insert(std::pair<VkPipelineKey, PipelineData>{key, PipelineData{std::move(pipeline), Uniforms}});
		return ptr;
	}
	else
	{
		Uniforms = item->second.Uniforms;
		return item->second.pipeline.get();
	}
}

std::unique_ptr<VulkanPipeline> VkRenderPassSetup::CreatePipeline(const VkPipelineKey &key, UniformStructHolder &Uniforms)
{
	GraphicsPipelineBuilder builder;
	builder.Cache(fb->GetRenderPassManager()->GetCache());

	VkShaderProgram *program = fb->GetShaderManager()->Get(key.ShaderKey);
	builder.AddVertexShader(program->vert.get());
	if (program->frag)
		builder.AddFragmentShader(program->frag.get());

	const VkVertexFormat &vfmt = *fb->GetRenderPassManager()->GetVertexFormat(key.VertexFormat);

	for (int i = 0; i < vfmt.BufferStrides.size(); i++)
		builder.AddVertexBufferBinding(i, vfmt.BufferStrides[i]);

	const static VkFormat vkfmts[] = {
		VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_FORMAT_R32G32B32_SFLOAT,
		VK_FORMAT_R32G32_SFLOAT,
		VK_FORMAT_R32_SFLOAT,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_A2B10G10R10_SNORM_PACK32,
		VK_FORMAT_R8G8B8A8_UINT,
		VK_FORMAT_R32_SINT
	};

	bool inputLocations[VATTR_MAX] = {};

	for (size_t i = 0; i < vfmt.Attrs.size(); i++)
	{
		const auto &attr = vfmt.Attrs[i];
		builder.AddVertexAttribute(attr.location, attr.binding, vkfmts[attr.format], attr.offset);
		inputLocations[attr.location] = true;
	}

	// Vulkan requires an attribute binding for each location specified in the shader
	for (int i = 0; i < VATTR_MAX; i++)
	{
		if (!inputLocations[i])
			builder.AddVertexAttribute(i, 0, i != 8 ? VK_FORMAT_R32G32B32_SFLOAT : VK_FORMAT_R8G8B8A8_UINT, 0);
	}

	builder.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
	builder.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR);
	builder.AddDynamicState(VK_DYNAMIC_STATE_DEPTH_BIAS);
	builder.AddDynamicState(VK_DYNAMIC_STATE_STENCIL_REFERENCE);

	// Note: the actual values are ignored since we use dynamic viewport+scissor states
	builder.Viewport(0.0f, 0.0f, 320.0f, 200.0f);
	builder.Scissor(0, 0, 320, 200);

	static const VkPrimitiveTopology vktopology[] = {
		VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
	};

	static const VkStencilOp op2vk[] = { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_DECREMENT_AND_CLAMP };
	static const VkCompareOp depthfunc2vk[] = { VK_COMPARE_OP_LESS, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_ALWAYS };

	builder.Topology(vktopology[key.DrawType]);
	builder.DepthStencilEnable(key.DepthTest, key.DepthWrite, key.StencilTest);
	builder.DepthFunc(depthfunc2vk[key.DepthFunc]);
	if (fb->GetDevice()->EnabledFeatures.Features.depthClamp)
		builder.DepthClampEnable(key.DepthClamp);
	builder.DepthBias(key.DepthBias, 0.0f, 0.0f, 0.0f);

	// Note: CCW and CW is intentionally swapped here because the vulkan and opengl coordinate systems differ.
	// main.vp addresses this by patching up gl_Position.z, which has the side effect of flipping the sign of the front face calculations.
	builder.Cull(key.CullMode == Cull_None ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT, key.CullMode == Cull_CW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);

	builder.Stencil(VK_STENCIL_OP_KEEP, op2vk[key.StencilPassOp], VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0xffffffff, 0xffffffff, 0);

	ColorBlendAttachmentBuilder blendbuilder;
	blendbuilder.ColorWriteMask((VkColorComponentFlags)key.ColorMask);
	BlendMode(blendbuilder, key.RenderStyle);

	for (int i = 0; i < PassKey.DrawBuffers; i++)
		builder.AddColorBlendAttachment(blendbuilder.Create());

	builder.RasterizationSamples((VkSampleCountFlagBits)PassKey.Samples);

	builder.Layout(fb->GetRenderPassManager()->GetPipelineLayout(key.ShaderKey.UseLevelMesh, program->Uniforms.sz));
	builder.RenderPass(GetRenderPass(0));
	builder.DebugName("VkRenderPassSetup.Pipeline");

	Uniforms = program->Uniforms;

	return builder.Create(fb->GetDevice());
}

/////////////////////////////////////////////////////////////////////////////

ColorBlendAttachmentBuilder& BlendMode(ColorBlendAttachmentBuilder& builder, const FRenderStyle& style)
{
	// Just in case Vulkan doesn't do this optimization itself
	if (style.BlendOp == STYLEOP_Add && style.SrcAlpha == STYLEALPHA_One && style.DestAlpha == STYLEALPHA_Zero && style.Flags == 0)
	{
		return builder;
	}

	static const int blendstyles[] = {
		VK_BLEND_FACTOR_ZERO,
		VK_BLEND_FACTOR_ONE,
		VK_BLEND_FACTOR_SRC_ALPHA,
		VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		VK_BLEND_FACTOR_SRC_COLOR,
		VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
		VK_BLEND_FACTOR_DST_COLOR,
		VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
		VK_BLEND_FACTOR_DST_ALPHA,
		VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	};

	static const int renderops[] = {
		0, VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
	};

	int srcblend = blendstyles[style.SrcAlpha % STYLEALPHA_MAX];
	int dstblend = blendstyles[style.DestAlpha % STYLEALPHA_MAX];
	int blendequation = renderops[style.BlendOp & 15];

	if (blendequation == -1)	// This was a fuzz style.
	{
		srcblend = VK_BLEND_FACTOR_DST_COLOR;
		dstblend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendequation = VK_BLEND_OP_ADD;
	}

	return builder.BlendMode((VkBlendOp)blendequation, (VkBlendFactor)srcblend, (VkBlendFactor)dstblend);
}
