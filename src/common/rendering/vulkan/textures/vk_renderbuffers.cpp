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

#include "vk_renderbuffers.h"
#include "vulkan/vk_postprocess.h"
#include "vulkan/vk_renderdevice.h"
#include "vulkan/textures/vk_texture.h"
#include "vulkan/framebuffers/vk_framebuffer.h"
#include "vulkan/shaders/vk_shader.h"
#include "vulkan/commands/vk_commandbuffer.h"
#include "vulkan/pipelines/vk_pprenderpass.h"
#include <zvulkan/vulkanswapchain.h>
#include <zvulkan/vulkanbuilders.h>
#include "hw_cvars.h"

VkRenderBuffers::VkRenderBuffers(VulkanRenderDevice* fb) : fb(fb)
{
}

VkRenderBuffers::~VkRenderBuffers()
{
}

VkSampleCountFlagBits VkRenderBuffers::GetBestSampleCount()
{
	const auto &limits = fb->GetDevice()->PhysicalDevice.Properties.Properties.limits;
	VkSampleCountFlags deviceSampleCounts = limits.sampledImageColorSampleCounts & limits.sampledImageDepthSampleCounts & limits.sampledImageStencilSampleCounts;

	int requestedSamples = clamp((int)gl_multisample, 0, 64);

	int samples = 1;
	VkSampleCountFlags bit = VK_SAMPLE_COUNT_1_BIT;
	VkSampleCountFlags best = bit;
	while (samples <= requestedSamples)
	{
		if (deviceSampleCounts & bit)
		{
			best = bit;
		}
		samples <<= 1;
		bit <<= 1;
	}
	return (VkSampleCountFlagBits)best;
}

void VkRenderBuffers::BeginFrame(int width, int height, int sceneWidth, int sceneHeight)
{
	VkSampleCountFlagBits samples = GetBestSampleCount();

	if (width != mWidth || height != mHeight || mSamples != samples)
	{
		fb->GetCommands()->WaitForCommands(false);
		fb->GetRenderPassManager()->RenderBuffersReset();
	}

	if (width != mWidth || height != mHeight)
		CreatePipeline(width, height);

	if (width != mWidth || height != mHeight || mSamples != samples)
		CreateScene(width, height, samples);

	if (width != mWidth || height != mHeight)
	{
		CreateSceneZMinMax(width, height);
		CreateSceneLightTiles(width, height);
	}

	if (sceneWidth != mSceneWidth || sceneHeight != mSceneHeight)
	{
		SceneLinearDepth.Reset(fb);
		CreateSceneLinearDepth(std::max(sceneWidth, 1), std::max(sceneHeight, 1));
	}

	mWidth = width;
	mHeight = height;
	mSamples = samples;
	mSceneWidth = sceneWidth;
	mSceneHeight = sceneHeight;
}

VulkanFramebuffer* VkRenderBuffers::GetFramebuffer(const VkRenderPassKey& key)
{
	auto& framebuffer = SceneColor.RSFramebuffers[key];
	if (framebuffer)
		return framebuffer.get();

	FramebufferBuilder builder;
	builder.RenderPass(fb->GetRenderPassManager()->GetRenderPass(key)->GetRenderPass(0));
	builder.Size(GetWidth(), GetHeight());
	builder.AddAttachment(SceneColor.View.get());
	if (key.DrawBuffers > 1)
		builder.AddAttachment(SceneFog.View.get());
	if (key.DrawBuffers > 2)
		builder.AddAttachment(SceneNormal.View.get());
	if (key.DepthStencil)
		builder.AddAttachment(SceneDepthStencil.View.get());
	builder.DebugName("VkRenderPassSetup.Framebuffer");
	framebuffer = builder.Create(fb->GetDevice());
	return framebuffer.get();
}

VulkanFramebuffer* VkRenderBuffers::GetZMinMaxFramebuffer(int index)
{
	auto& framebuffer = SceneZMinMax[index].ZMinMaxFramebuffer;
	if (framebuffer)
		return framebuffer.get();

	int width = ((GetWidth() + 63) / 64 * 64) >> 1;
	int height = ((GetHeight() + 63) / 64 * 64) >> 1;

	width >>= index;
	height >>= index;

	FramebufferBuilder builder;
	builder.RenderPass(fb->GetRenderPassManager()->GetZMinMaxRenderPass());
	builder.Size(width, height);
	builder.AddAttachment(SceneZMinMax[index].View.get());
	builder.DebugName("VkRenderBuffers.ZMinMaxFramebuffer");
	framebuffer = builder.Create(fb->GetDevice());
	return framebuffer.get();
}

void VkRenderBuffers::CreatePipelineDepthStencil(int width, int height)
{
	PipelineDepthStencil.Image = ImageBuilder()
		.Size(width, height)
		.Format(fb->DepthStencilFormat)
		.Usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName("VkRenderBuffers.PipelineDepthStencil")
		.Create(fb->GetDevice());

	PipelineDepthStencil.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

	PipelineDepthStencil.View = ImageViewBuilder()
		.Image(PipelineDepthStencil.Image.get(), fb->DepthStencilFormat, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
		.DebugName("VkRenderBuffers.PipelineDepthStencilView")
		.Create(fb->GetDevice());

	PipelineDepthStencil.DepthOnlyView = ImageViewBuilder()
		.Image(PipelineDepthStencil.Image.get(), fb->DepthStencilFormat, VK_IMAGE_ASPECT_DEPTH_BIT)
		.DebugName("VkRenderBuffers.PipelineDepthView")
		.Create(fb->GetDevice());
}

void VkRenderBuffers::CreatePipeline(int width, int height)
{
	for (int i = 0; i < NumPipelineImages; i++)
	{
		PipelineImage[i].Reset(fb);
	}
	PipelineDepthStencil.Reset(fb);

	CreatePipelineDepthStencil(width, height);

	VkImageTransition barrier;
	barrier.AddImage(&PipelineDepthStencil, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, true);
	for (int i = 0; i < NumPipelineImages; i++)
	{
		PipelineImage[i].Image = ImageBuilder()
			.Size(width, height)
			.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
			.Usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
			.DebugName("VkRenderBuffers.PipelineImage")
			.Create(fb->GetDevice());

		PipelineImage[i].View = ImageViewBuilder()
			.Image(PipelineImage[i].Image.get(), VK_FORMAT_R16G16B16A16_SFLOAT)
			.DebugName("VkRenderBuffers.PipelineView")
			.Create(fb->GetDevice());

		barrier.AddImage(&PipelineImage[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
	}
	barrier.Execute(fb->GetCommands()->GetDrawCommands());
}

void VkRenderBuffers::CreateScene(int width, int height, VkSampleCountFlagBits samples)
{
	SceneColor.Reset(fb);
	SceneDepthStencil.Reset(fb);
	SceneNormal.Reset(fb);
	SceneFog.Reset(fb);

	CreateSceneColor(width, height, samples);
	CreateSceneDepthStencil(width, height, samples);
	CreateSceneNormal(width, height, samples);
	CreateSceneFog(width, height, samples);

	VkImageTransition()
		.AddImage(&SceneColor, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true)
		.AddImage(&SceneDepthStencil, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, true)
		.AddImage(&SceneNormal, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true)
		.AddImage(&SceneFog, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true)
		.Execute(fb->GetCommands()->GetDrawCommands());
}

void VkRenderBuffers::CreateSceneColor(int width, int height, VkSampleCountFlagBits samples)
{
	SceneColor.Image = ImageBuilder()
		.Size(width, height)
		.Samples(samples)
		.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
		.Usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
		.DebugName("VkRenderBuffers.SceneColor")
		.Create(fb->GetDevice());

	SceneColor.View = ImageViewBuilder()
		.Image(SceneColor.Image.get(), VK_FORMAT_R16G16B16A16_SFLOAT)
		.DebugName("VkRenderBuffers.SceneColorView")
		.Create(fb->GetDevice());
}

void VkRenderBuffers::CreateSceneDepthStencil(int width, int height, VkSampleCountFlagBits samples)
{
	SceneDepthStencil.Image = ImageBuilder()
		.Size(width, height)
		.Samples(samples)
		.Format(fb->DepthStencilFormat)
		.Usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName("VkRenderBuffers.SceneDepthStencil")
		.Create(fb->GetDevice());

	SceneDepthStencil.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

	SceneDepthStencil.View = ImageViewBuilder()
		.Image(SceneDepthStencil.Image.get(), fb->DepthStencilFormat, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
		.DebugName("VkRenderBuffers.SceneDepthStencilView")
		.Create(fb->GetDevice());

	SceneDepthStencil.DepthOnlyView = ImageViewBuilder()
		.Image(SceneDepthStencil.Image.get(), fb->DepthStencilFormat, VK_IMAGE_ASPECT_DEPTH_BIT)
		.DebugName("VkRenderBuffers.SceneDepthView")
		.Create(fb->GetDevice());
}

void VkRenderBuffers::CreateSceneFog(int width, int height, VkSampleCountFlagBits samples)
{
	SceneFog.Image = ImageBuilder()
		.Size(width, height)
		.Samples(samples)
		.Format(VK_FORMAT_R8G8B8A8_UNORM)
		.Usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName("VkRenderBuffers.SceneFog")
		.Create(fb->GetDevice());

	SceneFog.View = ImageViewBuilder()
		.Image(SceneFog.Image.get(), VK_FORMAT_R8G8B8A8_UNORM)
		.DebugName("VkRenderBuffers.SceneFogView")
		.Create(fb->GetDevice());
}

void VkRenderBuffers::CreateSceneLinearDepth(int width, int height)
{
	SceneLinearDepth.Image = ImageBuilder()
		.Size(width, height)
		.Format(VK_FORMAT_R32_SFLOAT)
		.Usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName("VkRenderBuffers.SceneLinearDepth")
		.Create(fb->GetDevice());

	SceneLinearDepth.View = ImageViewBuilder()
		.Image(SceneLinearDepth.Image.get(), VK_FORMAT_R32_SFLOAT)
		.DebugName("VkRenderBuffers.SceneLinearDepthView")
		.Create(fb->GetDevice());

	VkImageTransition()
		.AddImage(&SceneLinearDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true)
		.Execute(fb->GetCommands()->GetDrawCommands());
}

void VkRenderBuffers::CreateSceneZMinMax(int width, int height)
{
	width = (width + 63) / 64 * 64;
	height = (height + 63) / 64 * 64;

	for (int i = 0; i < 6; i++)
	{
		width >>= 1;
		height >>= 1;

		SceneZMinMax[i].Image = ImageBuilder()
			.Size(width, height)
			.Format(VK_FORMAT_R32G32_SFLOAT)
			.Usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)
			.DebugName("VkRenderBuffers.SceneZMinMax")
			.Create(fb->GetDevice());

		SceneZMinMax[i].View = ImageViewBuilder()
			.Image(SceneZMinMax[i].Image.get(), VK_FORMAT_R32G32_SFLOAT)
			.DebugName("VkRenderBuffers.SceneZMinMaxView")
			.Create(fb->GetDevice());
	}
}

void VkRenderBuffers::CreateSceneLightTiles(int width, int height)
{
	width = (width + 63) / 64;
	height = (height + 63) / 64;

	// Make room for 16 lights plus the lightdata header
	size_t blockSize = (4 * sizeof(int)) + 16 * sizeof(FDynLightInfo);

	SceneLightTiles = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		.Size(width * height * blockSize)
		.DebugName("VkRenderBuffers.SceneLightTiles")
		.Create(fb->GetDevice());
}

void VkRenderBuffers::CreateSceneNormal(int width, int height, VkSampleCountFlagBits samples)
{
	SceneNormal.Image = ImageBuilder()
		.Size(width, height)
		.Samples(samples)
		.Format(fb->NormalFormat)
		.Usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName("VkRenderBuffers.SceneNormal")
		.Create(fb->GetDevice());

	SceneNormal.View = ImageViewBuilder()
		.Image(SceneNormal.Image.get(), fb->NormalFormat)
		.DebugName("VkRenderBuffers.SceneNormalView")
		.Create(fb->GetDevice());
}

VulkanFramebuffer* VkRenderBuffers::GetOutput(VkPPRenderPassSetup* passSetup, const PPOutput& output, WhichDepthStencil stencilTest, int& framebufferWidth, int& framebufferHeight)
{
	VkTextureImage* tex = fb->GetTextureManager()->GetTexture(output.Type, output.Texture);

	VkImageView view;
	std::unique_ptr<VulkanFramebuffer>* framebufferptr = nullptr;
	int w, h;
	if (tex)
	{
		VkImageTransition imageTransition;
		imageTransition.AddImage(tex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, output.Type == PPTextureType::NextPipelineTexture);
		if (stencilTest == WhichDepthStencil::Scene)
			imageTransition.AddImage(&fb->GetBuffers()->SceneDepthStencil, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false);

		if (stencilTest == WhichDepthStencil::Pipeline)
			imageTransition.AddImage(&fb->GetBuffers()->PipelineDepthStencil, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false);

		imageTransition.Execute(fb->GetCommands()->GetDrawCommands());

		view = tex->View->view;
		w = tex->Image->width;
		h = tex->Image->height;
		framebufferptr = &tex->PPFramebuffer;
	}
	else
	{
		view = fb->GetFramebufferManager()->SwapChain->GetImageView(fb->GetFramebufferManager()->PresentImageIndex)->view;
		framebufferptr = &fb->GetFramebufferManager()->Framebuffers[fb->GetFramebufferManager()->PresentImageIndex];
		w = fb->GetFramebufferManager()->SwapChain->Width();
		h = fb->GetFramebufferManager()->SwapChain->Height();
	}

	auto& framebuffer = *framebufferptr;
	if (!framebuffer)
	{
		FramebufferBuilder builder;
		builder.RenderPass(passSetup->RenderPass.get());
		builder.Size(w, h);
		builder.AddAttachment(view);
		if (stencilTest == WhichDepthStencil::Scene)
			builder.AddAttachment(fb->GetBuffers()->SceneDepthStencil.View.get());
		if (stencilTest == WhichDepthStencil::Pipeline)
			builder.AddAttachment(fb->GetBuffers()->PipelineDepthStencil.View.get());
		builder.DebugName("PPOutputFB");
		framebuffer = builder.Create(fb->GetDevice());
	}

	framebufferWidth = w;
	framebufferHeight = h;
	return framebuffer.get();
}
