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

#include <zvulkan/vulkanobjects.h>

#include <inttypes.h>

#include "v_video.h"
#include "m_png.h"

#include "r_videoscale.h"
#include "i_time.h"
#include "v_text.h"
#include "version.h"
#include "v_draw.h"

#include "hw_clock.h"
#include "hw_vrmodes.h"
#include "hw_cvars.h"
#include "hw_skydome.h"
#include "flatvertices.h"

#include "vk_renderdevice.h"
#include "vulkan/vk_renderstate.h"
#include "vulkan/vk_postprocess.h"
#include "vulkan/accelstructs/vk_raytrace.h"
#include "vulkan/accelstructs/vk_lightmap.h"
#include "vulkan/pipelines/vk_renderpass.h"
#include "vulkan/descriptorsets/vk_descriptorset.h"
#include "vulkan/shaders/vk_shader.h"
#include "vulkan/samplers/vk_samplers.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include "vulkan/textures/vk_hwtexture.h"
#include "vulkan/textures/vk_texture.h"
#include "vulkan/framebuffers/vk_framebuffer.h"
#include "vulkan/commands/vk_commandbuffer.h"
#include "vulkan/buffers/vk_hwbuffer.h"
#include "vulkan/buffers/vk_buffer.h"
#include "vulkan/buffers/vk_rsbuffers.h"
#include <zvulkan/vulkanswapchain.h>
#include <zvulkan/vulkanbuilders.h>
#include <zvulkan/vulkansurface.h>
#include <zvulkan/vulkancompatibledevice.h>
#include "engineerrors.h"
#include "c_dispatch.h"

FString JitCaptureStackTrace(int framesToSkip, bool includeNativeFrames, int maxFrames = -1);

EXTERN_CVAR(Int, gl_tonemap)
EXTERN_CVAR(Int, screenblocks)
EXTERN_CVAR(Bool, cl_capfps)

// Physical device info
static std::vector<VulkanCompatibleDevice> SupportedDevices;

CUSTOM_CVAR(Bool, vk_debug, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

CVAR(Bool, vk_debug_callstack, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

CUSTOM_CVAR(Int, vk_device, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

CCMD(vk_listdevices)
{
	for (size_t i = 0; i < SupportedDevices.size(); i++)
	{
		Printf("#%d - %s\n", (int)i, SupportedDevices[i].Device->Properties.Properties.deviceName);
	}
}

void VulkanError(const char* text)
{
	throw CVulkanError(text);
}

void VulkanPrintLog(const char* typestr, const std::string& msg)
{
	bool showcallstack = strstr(typestr, "error") != nullptr;

	if (showcallstack)
		Printf("\n");

	Printf(TEXTCOLOR_RED "[%s] ", typestr);
	Printf(TEXTCOLOR_WHITE "%s\n", msg.c_str());

	if (vk_debug_callstack && showcallstack)
	{
		FString callstack = JitCaptureStackTrace(0, true, 5);
		if (!callstack.IsEmpty())
			Printf("%s\n", callstack.GetChars());
	}
}

VulkanRenderDevice::VulkanRenderDevice(void *hMonitor, bool fullscreen, std::shared_ptr<VulkanSurface> surface) : SystemBaseFrameBuffer(hMonitor, fullscreen)
{
	VulkanDeviceBuilder builder;
	builder.OptionalRayQuery();
	builder.Surface(surface);
	builder.SelectDevice(vk_device);
	SupportedDevices = builder.FindDevices(surface->Instance);
	mDevice = builder.Create(surface->Instance);
}

VulkanRenderDevice::~VulkanRenderDevice()
{
	vkDeviceWaitIdle(mDevice->device); // make sure the GPU is no longer using any objects before RAII tears them down

	delete mSkyData;
	delete mShadowMap;

	if (mDescriptorSetManager)
		mDescriptorSetManager->Deinit();
	if (mTextureManager)
		mTextureManager->Deinit();
	if (mBufferManager)
		mBufferManager->Deinit();
	if (mShaderManager)
		mShaderManager->Deinit();

	mCommands->DeleteFrameObjects();
}

void VulkanRenderDevice::InitializeState()
{
	static bool first = true;
	if (first)
	{
		PrintStartupLog();
		first = false;
	}

	// Use the same names here as OpenGL returns.
	switch (mDevice->PhysicalDevice.Properties.Properties.vendorID)
	{
	case 0x1002: vendorstring = "ATI Technologies Inc.";     break;
	case 0x10DE: vendorstring = "NVIDIA Corporation";  break;
	case 0x8086: vendorstring = "Intel";   break;
	default:     vendorstring = "Unknown"; break;
	}

	hwcaps = RFL_SHADER_STORAGE_BUFFER | RFL_BUFFER_STORAGE;
	glslversion = 4.50f;
	uniformblockalignment = (unsigned int)mDevice->PhysicalDevice.Properties.Properties.limits.minUniformBufferOffsetAlignment;
	maxuniformblock = mDevice->PhysicalDevice.Properties.Properties.limits.maxUniformBufferRange;

	mCommands.reset(new VkCommandBufferManager(this));

	mSamplerManager.reset(new VkSamplerManager(this));
	mTextureManager.reset(new VkTextureManager(this));
	mFramebufferManager.reset(new VkFramebufferManager(this));
	mBufferManager.reset(new VkBufferManager(this));

	mScreenBuffers.reset(new VkRenderBuffers(this));
	mSaveBuffers.reset(new VkRenderBuffers(this));
	mActiveRenderBuffers = mScreenBuffers.get();

	mPostprocess.reset(new VkPostprocess(this));
	mDescriptorSetManager.reset(new VkDescriptorSetManager(this));
	mRenderPassManager.reset(new VkRenderPassManager(this));
	mRaytrace.reset(new VkRaytrace(this));
	mLightmap.reset(new VkLightmap(this));

	mBufferManager->Init();

	mSkyData = new FSkyVertexBuffer(this);
	mShadowMap = new ShadowMap(this);

	mShaderManager.reset(new VkShaderManager(this));
	mDescriptorSetManager->Init();

	for (int threadIndex = 0; threadIndex < MaxThreads; threadIndex++)
	{
#ifdef __APPLE__
		mRenderState.push_back(std::make_unique<VkRenderStateMolten>(this, 0));
#else
		mRenderState.push_back(std::make_unique<VkRenderState>(this, 0));
#endif
	}
}

void VulkanRenderDevice::Update()
{
	twoD.Reset();
	Flush3D.Reset();

	Flush3D.Clock();

	GetPostprocess()->SetActiveRenderTarget();

	Draw2D();
	twod->Clear();

	for (auto& renderstate : mRenderState)
	{
		renderstate->EndRenderPass();
		renderstate->EndFrame();
	}

	Flush3D.Unclock();

	mCommands->WaitForCommands(true);
	mCommands->UpdateGpuStats();

	SystemBaseFrameBuffer::Update();
}

bool VulkanRenderDevice::CompileNextShader()
{
	return mShaderManager->CompileNextShader();
}

void VulkanRenderDevice::RenderTextureView(FCanvasTexture* tex, std::function<void(IntRect &)> renderFunc)
{
	auto BaseLayer = static_cast<VkHardwareTexture*>(tex->GetHardwareTexture(0, 0));

	VkTextureImage *image = BaseLayer->GetImage(tex, 0, 0);
	VkTextureImage *depthStencil = BaseLayer->GetDepthStencil(tex);

	for (auto& renderstate : mRenderState)
	{
		renderstate->EndRenderPass();
	}

	VkImageTransition()
		.AddImage(image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, false)
		.Execute(mCommands->GetDrawCommands());

	for (auto& renderstate : mRenderState)
	{
		renderstate->SetRenderTarget(image, depthStencil->View.get(), image->Image->width, image->Image->height, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT);
	}

	IntRect bounds;
	bounds.left = bounds.top = 0;
	bounds.width = min(tex->GetWidth(), image->Image->width);
	bounds.height = min(tex->GetHeight(), image->Image->height);

	renderFunc(bounds);

	for (auto& renderstate : mRenderState)
	{
		renderstate->EndRenderPass();
	}

	VkImageTransition()
		.AddImage(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false)
		.Execute(mCommands->GetDrawCommands());

	for (auto& renderstate : mRenderState)
	{
		renderstate->SetRenderTarget(&GetBuffers()->SceneColor, GetBuffers()->SceneDepthStencil.View.get(), GetBuffers()->GetWidth(), GetBuffers()->GetHeight(), VK_FORMAT_R16G16B16A16_SFLOAT, GetBuffers()->GetSceneSamples());
	}

	tex->SetUpdated(true);
}

void VulkanRenderDevice::PostProcessScene(bool swscene, int fixedcm, float flash, const std::function<void()> &afterBloomDrawEndScene2D)
{
	if (!swscene) mPostprocess->BlitSceneToPostprocess(); // Copy the resulting scene to the current post process texture
	mPostprocess->PostProcessScene(fixedcm, flash, afterBloomDrawEndScene2D);
}

const char* VulkanRenderDevice::DeviceName() const
{
	const auto &props = mDevice->PhysicalDevice.Properties;
	return props.Properties.deviceName;
}

void VulkanRenderDevice::SetVSync(bool vsync)
{
	mVSync = vsync;
}

void VulkanRenderDevice::PrecacheMaterial(FMaterial *mat, int translation)
{
	if (mat->Source()->GetUseType() == ETextureType::SWCanvas) return;

	MaterialLayerInfo* layer;

	auto systex = static_cast<VkHardwareTexture*>(mat->GetLayer(0, translation, &layer));
	systex->GetImage(layer->layerTexture, translation, layer->scaleFlags);

	int numLayers = mat->NumLayers();
	for (int i = 1; i < numLayers; i++)
	{
		auto syslayer = static_cast<VkHardwareTexture*>(mat->GetLayer(i, 0, &layer));
		syslayer->GetImage(layer->layerTexture, 0, layer->scaleFlags);
	}
}

IHardwareTexture *VulkanRenderDevice::CreateHardwareTexture(int numchannels)
{
	return new VkHardwareTexture(this, numchannels);
}

FMaterial* VulkanRenderDevice::CreateMaterial(FGameTexture* tex, int scaleflags)
{
	return new VkMaterial(this, tex, scaleflags);
}

IBuffer*VulkanRenderDevice::CreateVertexBuffer(int numBindingPoints, int numAttributes, size_t stride, const FVertexBufferAttribute* attrs)
{
	return GetBufferManager()->CreateVertexBuffer(numBindingPoints, numAttributes, stride, attrs);
}

IBuffer*VulkanRenderDevice::CreateIndexBuffer()
{
	return GetBufferManager()->CreateIndexBuffer();
}

void VulkanRenderDevice::SetTextureFilterMode()
{
	if (mSamplerManager)
	{
		mDescriptorSetManager->ResetHWTextureSets();
		mSamplerManager->ResetHWSamplers();
	}
}

void VulkanRenderDevice::StartPrecaching()
{
	// Destroy the texture descriptors to avoid problems with potentially stale textures.
	mDescriptorSetManager->ResetHWTextureSets();
}

void VulkanRenderDevice::BlurScene(float amount)
{
	if (mPostprocess)
		mPostprocess->BlurScene(amount);
}

void VulkanRenderDevice::UpdatePalette()
{
	if (mPostprocess)
		mPostprocess->ClearTonemapPalette();
}

FTexture *VulkanRenderDevice::WipeStartScreen()
{
	SetViewportRects(nullptr);

	auto tex = new FWrapperTexture(mScreenViewport.width, mScreenViewport.height, 1);
	auto systex = static_cast<VkHardwareTexture*>(tex->GetSystemTexture());

	systex->CreateWipeTexture(mScreenViewport.width, mScreenViewport.height, "WipeStartScreen");

	return tex;
}

FTexture *VulkanRenderDevice::WipeEndScreen()
{
	GetPostprocess()->SetActiveRenderTarget();
	Draw2D();
	twod->Clear();

	auto tex = new FWrapperTexture(mScreenViewport.width, mScreenViewport.height, 1);
	auto systex = static_cast<VkHardwareTexture*>(tex->GetSystemTexture());

	systex->CreateWipeTexture(mScreenViewport.width, mScreenViewport.height, "WipeEndScreen");

	return tex;
}

void VulkanRenderDevice::CopyScreenToBuffer(int w, int h, uint8_t *data)
{
	VkTextureImage image;

	// Convert from rgba16f to rgba8 using the GPU:
	image.Image = ImageBuilder()
		.Format(VK_FORMAT_R8G8B8A8_UNORM)
		.Usage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.Size(w, h)
		.DebugName("CopyScreenToBuffer")
		.Create(mDevice.get());

	GetPostprocess()->BlitCurrentToImage(&image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	// Staging buffer for download
	auto staging = BufferBuilder()
		.Size(w * h * 4)
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU)
		.DebugName("CopyScreenToBuffer")
		.Create(mDevice.get());

	// Copy from image to buffer
	VkBufferImageCopy region = {};
	region.imageExtent.width = w;
	region.imageExtent.height = h;
	region.imageExtent.depth = 1;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	mCommands->GetDrawCommands()->copyImageToBuffer(image.Image->image, image.Layout, staging->buffer, 1, &region);

	// Submit command buffers and wait for device to finish the work
	mCommands->WaitForCommands(false);

	// Map and convert from rgba8 to rgb8
	uint8_t *dest = (uint8_t*)data;
	uint8_t *pixels = (uint8_t*)staging->Map(0, w * h * 4);
	int dindex = 0;
	for (int y = 0; y < h; y++)
	{
		int sindex = (h - y - 1) * w * 4;
		for (int x = 0; x < w; x++)
		{
			dest[dindex] = pixels[sindex];
			dest[dindex + 1] = pixels[sindex + 1];
			dest[dindex + 2] = pixels[sindex + 2];
			dindex += 3;
			sindex += 4;
		}
	}
	staging->Unmap();
}

void VulkanRenderDevice::SetActiveRenderTarget()
{
	mPostprocess->SetActiveRenderTarget();
}

TArray<uint8_t> VulkanRenderDevice::GetScreenshotBuffer(int &pitch, ESSType &color_type, float &gamma)
{
	int w = SCREENWIDTH;
	int h = SCREENHEIGHT;

	IntRect box;
	box.left = 0;
	box.top = 0;
	box.width = w;
	box.height = h;
	mPostprocess->DrawPresentTexture(box, true, true);

	TArray<uint8_t> ScreenshotBuffer(w * h * 3, true);
	CopyScreenToBuffer(w, h, ScreenshotBuffer.Data());

	pitch = w * 3;
	color_type = SS_RGB;
	gamma = 1.0f;
	return ScreenshotBuffer;
}

void VulkanRenderDevice::BeginFrame()
{
	if (levelMeshChanged)
	{
		levelMeshChanged = false;
		mRaytrace->SetLevelMesh(levelMesh);

		if (levelMesh && levelMesh->GetSurfaceCount() > 0)
		{
			levelMesh->UpdateLightLists();
			GetTextureManager()->CreateLightmap(levelMesh->LMTextureSize, levelMesh->LMTextureCount, std::move(levelMesh->LMTextureData));
			GetLightmap()->SetLevelMesh(levelMesh);
		}
	}

	SetViewportRects(nullptr);
	mCommands->BeginFrame();
	mTextureManager->BeginFrame();
	mScreenBuffers->BeginFrame(screen->mScreenViewport.width, screen->mScreenViewport.height, screen->mSceneViewport.width, screen->mSceneViewport.height);
	mSaveBuffers->BeginFrame(SAVEPICWIDTH, SAVEPICHEIGHT, SAVEPICWIDTH, SAVEPICHEIGHT);
	for (auto& renderstate : mRenderState)
		renderstate->BeginFrame();
	mDescriptorSetManager->BeginFrame();
	mLightmap->BeginFrame();
}

void VulkanRenderDevice::Draw2D()
{
	::Draw2D(twod, *RenderState(0));
}

void VulkanRenderDevice::WaitForCommands(bool finish)
{
	mCommands->WaitForCommands(finish);
}

void VulkanRenderDevice::ResetRenderStateCache()
{
	for (auto& renderstate : mRenderState)
	{
		renderstate->ResetCache();
	}
}

void VulkanRenderDevice::PrintStartupLog()
{
	const auto &props = mDevice->PhysicalDevice.Properties.Properties;

	FString deviceType;
	switch (props.deviceType)
	{
	case VK_PHYSICAL_DEVICE_TYPE_OTHER: deviceType = "other"; break;
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: deviceType = "integrated gpu"; break;
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: deviceType = "discrete gpu"; break;
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: deviceType = "virtual gpu"; break;
	case VK_PHYSICAL_DEVICE_TYPE_CPU: deviceType = "cpu"; break;
	default: deviceType.Format("%d", (int)props.deviceType); break;
	}

	FString apiVersion, driverVersion;
	apiVersion.Format("%d.%d.%d", VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
	driverVersion.Format("%d.%d.%d", VK_VERSION_MAJOR(props.driverVersion), VK_VERSION_MINOR(props.driverVersion), VK_VERSION_PATCH(props.driverVersion));

	Printf("Vulkan device: " TEXTCOLOR_ORANGE "%s\n", props.deviceName);
	Printf("Vulkan device type: %s\n", deviceType.GetChars());
	Printf("Vulkan version: %s (api) %s (driver)\n", apiVersion.GetChars(), driverVersion.GetChars());

	Printf(PRINT_LOG, "Vulkan extensions:");
	for (const VkExtensionProperties &p : mDevice->PhysicalDevice.Extensions)
	{
		Printf(PRINT_LOG, " %s", p.extensionName);
	}
	Printf(PRINT_LOG, "\n");

	const auto &limits = props.limits;
	Printf("Max. texture size: %d\n", limits.maxImageDimension2D);
	Printf("Max. uniform buffer range: %d\n", limits.maxUniformBufferRange);
	Printf("Min. uniform buffer offset alignment: %" PRIu64 "\n", limits.minUniformBufferOffsetAlignment);
}

void VulkanRenderDevice::SetLevelMesh(LevelMesh* mesh)
{
	levelMesh = mesh;
	levelMeshChanged = true;
}

void VulkanRenderDevice::UpdateLightmaps(const TArray<LevelMeshSurface*>& surfaces)
{
	if (surfaces.Size() > 0 && levelMesh)
	{
		GetLightmap()->Raytrace(surfaces);
	}
}

void VulkanRenderDevice::SetShadowMaps(const TArray<float>& lights, hwrenderer::LevelAABBTree* tree, bool newTree)
{
	auto buffers = GetBufferManager();

	buffers->Shadowmap.Lights->SetData(sizeof(float) * lights.Size(), lights.Data(), BufferUsageType::Stream);

	if (newTree)
	{
		buffers->Shadowmap.Nodes->SetData(tree->NodesSize(), tree->Nodes(), BufferUsageType::Static);
		buffers->Shadowmap.Lines->SetData(tree->LinesSize(), tree->Lines(), BufferUsageType::Static);
	}
	else if (tree->Update())
	{
		buffers->Shadowmap.Nodes->SetSubData(tree->DynamicNodesOffset(), tree->DynamicNodesSize(), tree->DynamicNodes());
		buffers->Shadowmap.Lines->SetSubData(tree->DynamicLinesOffset(), tree->DynamicLinesSize(), tree->DynamicLines());
	}

	mPostprocess->UpdateShadowMap();
}

void VulkanRenderDevice::SetSaveBuffers(bool yes)
{
	if (yes) mActiveRenderBuffers = mSaveBuffers.get();
	else mActiveRenderBuffers = mScreenBuffers.get();
}

void VulkanRenderDevice::ImageTransitionScene(bool unknown)
{
	mPostprocess->ImageTransitionScene(unknown);
}

FRenderState* VulkanRenderDevice::RenderState(int threadIndex)
{
	return mRenderState[threadIndex].get();
}

void VulkanRenderDevice::AmbientOccludeScene(float m5)
{
	mPostprocess->AmbientOccludeScene(m5);
}

void VulkanRenderDevice::SetSceneRenderTarget(bool useSSAO)
{
	for (auto& renderstate : mRenderState)
	{
		renderstate->SetRenderTarget(&GetBuffers()->SceneColor, GetBuffers()->SceneDepthStencil.View.get(), GetBuffers()->GetWidth(), GetBuffers()->GetHeight(), VK_FORMAT_R16G16B16A16_SFLOAT, GetBuffers()->GetSceneSamples());
	}
}
