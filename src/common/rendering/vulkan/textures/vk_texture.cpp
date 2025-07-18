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

#include "vk_texture.h"
#include "vk_hwtexture.h"
#include "vk_pptexture.h"
#include "vk_renderbuffers.h"
#include "vulkan/vk_postprocess.h"
#include "hw_cvars.h"
#include "fcolormap.h"

VkTextureManager::VkTextureManager(VulkanRenderDevice* fb) : fb(fb)
{
	CreateNullTexture();
	CreateBrdfLutTexture();
	CreateGamePalette();
	CreateShadowmap();
	CreateLightmap();
	CreateIrradiancemap();
	CreatePrefiltermap();
	StartWorkerThread();
}

VkTextureManager::~VkTextureManager()
{
	StopWorkerThread();
	while (!Textures.empty())
		RemoveTexture(Textures.back());
	while (!PPTextures.empty())
		RemovePPTexture(PPTextures.back());
}

void VkTextureManager::Deinit()
{
	while (!Textures.empty())
		RemoveTexture(Textures.back());
	while (!PPTextures.empty())
		RemovePPTexture(PPTextures.back());
}

void VkTextureManager::BeginFrame(int lightmapTextureSize, int lightmapCount)
{
	if (!Shadowmap.Image || Shadowmap.Image->width != gl_shadowmap_quality)
	{
		Shadowmap.Reset(fb);
		CreateShadowmap();
	}

	SetLightmapCount(lightmapTextureSize, lightmapCount);
}

void VkTextureManager::AddTexture(VkHardwareTexture* texture)
{
	texture->it = Textures.insert(Textures.end(), texture);
}

void VkTextureManager::RemoveTexture(VkHardwareTexture* texture)
{
	texture->Reset();
	texture->fb = nullptr;
	Textures.erase(texture->it);

	// Make sure no pending uploads access the texture after it has been destroyed by the hwrenderer
	for (auto it = PendingUploads.begin(); it != PendingUploads.end(); ++it)
	{
		if (it->second == texture)
		{
			PendingUploads.erase(it);
			break;
		}
	}
}

void VkTextureManager::AddPPTexture(VkPPTexture* texture)
{
	texture->it = PPTextures.insert(PPTextures.end(), texture);
}

void VkTextureManager::RemovePPTexture(VkPPTexture* texture)
{
	texture->Reset();
	texture->fb = nullptr;
	PPTextures.erase(texture->it);
}

VkTextureImage* VkTextureManager::GetTexture(const PPTextureType& type, PPTexture* pptexture)
{
	if (type == PPTextureType::CurrentPipelineTexture || type == PPTextureType::NextPipelineTexture)
	{
		int idx = fb->GetPostprocess()->GetCurrentPipelineImage();
		if (type == PPTextureType::NextPipelineTexture)
			idx = (idx + 1) % VkRenderBuffers::NumPipelineImages;

		return &fb->GetBuffers()->PipelineImage[idx];
	}
	else if (type == PPTextureType::PPTexture)
	{
		auto vktex = GetVkTexture(pptexture);
		return &vktex->TexImage;
	}
	else if (type == PPTextureType::SceneColor)
	{
		return &fb->GetBuffers()->SceneColor;
	}
	else if (type == PPTextureType::SceneNormal)
	{
		return &fb->GetBuffers()->SceneNormal;
	}
	else if (type == PPTextureType::SceneFog)
	{
		return &fb->GetBuffers()->SceneFog;
	}
	else if (type == PPTextureType::SceneDepth)
	{
		return &fb->GetBuffers()->SceneDepthStencil;
	}
	else if (type == PPTextureType::SceneLinearDepth)
	{
		return &fb->GetBuffers()->SceneLinearDepth;
	}
	else if (type == PPTextureType::ShadowMap)
	{
		return &Shadowmap;
	}
	else if (type == PPTextureType::SwapChain)
	{
		return nullptr;
	}
	else
	{
		I_FatalError("VkPPRenderState::GetTexture not implemented yet for this texture type");
		return nullptr;
	}
}

VkFormat VkTextureManager::GetTextureFormat(PPTexture* texture)
{
	return GetVkTexture(texture)->Format;
}

VkPPTexture* VkTextureManager::GetVkTexture(PPTexture* texture)
{
	if (!texture->Backend)
		texture->Backend = std::make_unique<VkPPTexture>(fb, texture);
	return static_cast<VkPPTexture*>(texture->Backend.get());
}

void VkTextureManager::CreateNullTexture()
{
	NullTexture = ImageBuilder()
		.Format(VK_FORMAT_R8G8B8A8_UNORM)
		.Size(1, 1)
		.Usage(VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName("VkDescriptorSetManager.NullTexture")
		.Create(fb->GetDevice());

	NullTextureView = ImageViewBuilder()
		.Image(NullTexture.get(), VK_FORMAT_R8G8B8A8_UNORM)
		.DebugName("VkDescriptorSetManager.NullTextureView")
		.Create(fb->GetDevice());

	PipelineBarrier()
		.AddImage(NullTexture.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
		.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void VkTextureManager::CreateBrdfLutTexture()
{
	const char* lumpname = "bdrf.lut";
	int lump = fileSystem.CheckNumForFullName(lumpname, 0);
	if (lump == -1) I_FatalError("Unable to load '%s'", lumpname);
	auto fd = fileSystem.ReadFile(lump);
	if (fd.size() != 512 * 512 * 2 * sizeof(uint16_t))
		I_FatalError("Unexpected file size for 'bdrf.lut'");

	BrdfLutTexture = ImageBuilder()
		.Format(VK_FORMAT_R16G16_SFLOAT)
		.Size(512, 512)
		.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.DebugName("VkDescriptorSetManager.BrdfLutTexture")
		.Create(fb->GetDevice());

	BrdfLutTextureView = ImageViewBuilder()
		.Image(BrdfLutTexture.get(), VK_FORMAT_R16G16_SFLOAT)
		.DebugName("VkDescriptorSetManager.BrdfLutTextureView")
		.Create(fb->GetDevice());

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	PipelineBarrier()
		.AddImage(BrdfLutTexture.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	auto stagingBuffer = BufferBuilder()
		.Size(512 * 512 * 2 * sizeof(uint16_t))
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.DebugName("VkDescriptorSetManager.BrdfLutTextureStagingBuffer")
		.Create(fb->GetDevice());

	void* data = stagingBuffer->Map(0, fd.size());
	memcpy(data, fd.data(), fd.size());
	stagingBuffer->Unmap();

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.depth = 1;
	region.imageExtent.width = 512;
	region.imageExtent.height = 512;
	cmdbuffer->copyBufferToImage(stagingBuffer->buffer, BrdfLutTexture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	fb->GetCommands()->TransferDeleteList->Add(std::move(stagingBuffer));

	PipelineBarrier()
		.AddImage(BrdfLutTexture.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void VkTextureManager::CreateGamePalette()
{
	GamePalette = ImageBuilder()
		.Format(VK_FORMAT_B8G8R8A8_UNORM)
		.Size(512, 512)
		.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.DebugName("VkDescriptorSetManager.GamePalette")
		.Create(fb->GetDevice());

	GamePaletteView = ImageViewBuilder()
		.Image(GamePalette.get(), VK_FORMAT_B8G8R8A8_UNORM)
		.DebugName("VkDescriptorSetManager.GamePaletteView")
		.Create(fb->GetDevice());
}

void VkTextureManager::SetGamePalette()
{
	std::shared_ptr<void> data(new uint32_t[512 * 512], [](void* p) { delete[](uint32_t*)p; });
	uint8_t* lut = (uint8_t*)data.get();
	for (int r = 0; r < 64; r++)
	{
		for (int g = 0; g < 64; g++)
		{
			for (int b = 0; b < 64; b++)
			{
				// Do not tonemap this. Must match the RGB666 lookup table from the software renderer exactly.
				PalEntry color = GPalette.BaseColors[ColorMatcher.Pick((r << 2) | (r >> 4), (g << 2) | (g >> 4), (b << 2) | (b >> 4))];
				int index = ((r * 64 + g) * 64 + b) * 4;
				lut[index] = color.b;
				lut[index + 1] = color.g;
				lut[index + 2] = color.r;
				lut[index + 3] = 255;
			}
		}
	}

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	PipelineBarrier()
		.AddImage(GamePalette.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	auto stagingBuffer = BufferBuilder()
		.Size(512 * 512 * 4)
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.DebugName("VkDescriptorSetManager.GamePaletteStagingBuffer")
		.Create(fb->GetDevice());

	void* dest = stagingBuffer->Map(0, 512 * 512 * 4);
	memcpy(dest, data.get(), 512 * 512 * 4);
	stagingBuffer->Unmap();

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.depth = 1;
	region.imageExtent.width = 512;
	region.imageExtent.height = 512;
	cmdbuffer->copyBufferToImage(stagingBuffer->buffer, GamePalette->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	fb->GetCommands()->TransferDeleteList->Add(std::move(stagingBuffer));

	PipelineBarrier()
		.AddImage(GamePalette.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

VulkanImageView* VkTextureManager::GetSWColormapView(FSWColormap* colormap)
{
	if (colormap->Renderdev.textureIndex != -1)
		return Colormaps[colormap->Renderdev.textureIndex].View.get();
	
	SWColormapTexture tex;

	tex.Texture = ImageBuilder()
		.Format(VK_FORMAT_B8G8R8A8_UNORM)
		.Size(256, 33)
		.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.DebugName("VkDescriptorSetManager.SWColormap")
		.Create(fb->GetDevice());

	tex.View = ImageViewBuilder()
		.Image(tex.Texture.get(), VK_FORMAT_B8G8R8A8_UNORM)
		.DebugName("VkDescriptorSetManager.SWColormapView")
		.Create(fb->GetDevice());

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	PipelineBarrier()
		.AddImage(tex.Texture.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	auto stagingBuffer = BufferBuilder()
		.Size(256 * 33 * sizeof(uint32_t))
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.DebugName("VkDescriptorSetManager.SWColormapStagingBuffer")
		.Create(fb->GetDevice());

	const uint8_t* src = colormap->Maps;
	uint32_t* data = static_cast<uint32_t*>(stagingBuffer->Map(0, 256 * 33 * sizeof(uint32_t)));
	for (int i = 0; i < 256 * 32; i++)
	{
		data[i] = GPalette.BaseColors[src[i]].d;
	}

	// Always include the game palette as we need it for dynlights (they ignore the fog for stupid reasons)
	memcpy(data + 256 * 32, GPalette.BaseColors, 256 * sizeof(uint32_t));
	
	stagingBuffer->Unmap();

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.depth = 1;
	region.imageExtent.width = 256;
	region.imageExtent.height = 33;
	cmdbuffer->copyBufferToImage(stagingBuffer->buffer, tex.Texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	fb->GetCommands()->TransferDeleteList->Add(std::move(stagingBuffer));

	PipelineBarrier()
		.AddImage(tex.Texture.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	colormap->Renderdev.textureIndex = Colormaps.size();
	Colormaps.push_back(std::move(tex));

	return Colormaps[colormap->Renderdev.textureIndex].View.get();
}

void VkTextureManager::CreateShadowmap()
{
	Shadowmap.Image = ImageBuilder()
		.Size(gl_shadowmap_quality, 1024)
		.Format(VK_FORMAT_R32_SFLOAT)
		.Usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName("VkRenderBuffers.Shadowmap")
		.Create(fb->GetDevice());

	Shadowmap.View = ImageViewBuilder()
		.Image(Shadowmap.Image.get(), VK_FORMAT_R32_SFLOAT)
		.DebugName("VkRenderBuffers.ShadowmapView")
		.Create(fb->GetDevice());

	VkImageTransition()
		.AddImage(&Shadowmap, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true)
		.Execute(fb->GetCommands()->GetDrawCommands());
}

void VkTextureManager::CreateLightmap()
{
	TArray<uint16_t> data;
	data.Push(0);
	data.Push(0);
	data.Push(0);
	CreateLightmap(1, 1, std::move(data));
}

void VkTextureManager::CreateIrradiancemap()
{
	TArray<uint16_t> data(IrradiancemapSize * IrradiancemapSize * 6 * 3, true);
	memset(data.data(), 0, data.size() * sizeof(uint16_t));
	UploadIrradiancemap(1, std::move(data));
}

void VkTextureManager::CreatePrefiltermap()
{
	int texsize = 0;
	for (int level = 0; level <= MAX_REFLECTION_LOD; level++)
	{
		int mipsize = PrefiltermapSize >> level;
		texsize += mipsize * mipsize;
	}

	TArray<uint16_t> data(texsize * 6 * 3, true);
	memset(data.data(), 0, data.size() * sizeof(uint16_t));
	UploadPrefiltermap(1, std::move(data));
}

void VkTextureManager::ResetLightProbes()
{
	// Special thanks to Khronos for making it so simple to clear an image...

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();
	int miplevels = MAX_REFLECTION_LOD + 1;

	VkImageTransition barrier0;
	for (auto& map : Prefiltermaps)
	{
		if (map.Image)
			barrier0.AddImage(&map, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true, 0, miplevels, 0, 6);
	}
	for (auto& map : Irradiancemaps)
	{
		if (map.Image)
			barrier0.AddImage(&map, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true, 0, 1, 0, 6);
	}
	barrier0.Execute(cmdbuffer);

	VkClearColorValue color = {};
	VkImageSubresourceRange range = {};
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	range.layerCount = 6;
	range.levelCount = miplevels;
	for (auto& map : Prefiltermaps)
	{
		if (map.Image)
			cmdbuffer->clearColorImage(map.Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
	}

	range.levelCount = 1;
	for (auto& map : Irradiancemaps)
	{
		if (map.Image)
			cmdbuffer->clearColorImage(map.Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
	}

	VkImageTransition barrier1;
	for (auto& map : Prefiltermaps)
	{
		if (map.Image)
			barrier1.AddImage(&map, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, 0, miplevels, 0, 6);
	}
	for (auto& map : Irradiancemaps)
	{
		if (map.Image)
			barrier1.AddImage(&map, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, 0, 1, 0, 6);
	}
	barrier1.Execute(cmdbuffer);
}

void VkTextureManager::CopyIrradiancemap(const std::vector<std::unique_ptr<VulkanImage>>& probes)
{
	CheckIrradiancemapSize(probes.size());

	auto cmdbuffer = fb->GetCommands()->GetDrawCommands();

	VkImageTransition barrier0;
	for (size_t i = 0; i < probes.size(); i++)
		barrier0.AddImage(&Irradiancemaps[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true, 0, 1, 0, 6);
	barrier0.Execute(cmdbuffer);

	VkImageCopy region = {};
	region.extent.width = IrradiancemapSize;
	region.extent.height = IrradiancemapSize;
	region.extent.depth = 1;
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = 6;
	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.dstSubresource.layerCount = 6;

	for (size_t i = 0; i < probes.size(); i++)
	{
		cmdbuffer->copyImage(probes[i]->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Irradiancemaps[i].Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	}

	VkImageTransition barrier1;
	for (size_t i = 0; i < probes.size(); i++)
		barrier1.AddImage(&Irradiancemaps[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, 0, 1, 0, 6);
	barrier1.Execute(cmdbuffer);
}

void VkTextureManager::CopyPrefiltermap(const std::vector<std::unique_ptr<VulkanImage>>& probes)
{
	CheckPrefiltermapSize(probes.size());

	auto cmdbuffer = fb->GetCommands()->GetDrawCommands();
	int miplevels = MAX_REFLECTION_LOD + 1;

	VkImageTransition barrier0;
	for (size_t i = 0; i < probes.size(); i++)
		barrier0.AddImage(&Prefiltermaps[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true, 0, miplevels, 0, 6);
	barrier0.Execute(cmdbuffer);

	std::vector<VkImageCopy> regions;
	for (int level = 0; level < miplevels; level++)
	{
		VkImageCopy region = {};
		region.extent.width = PrefiltermapSize >> level;
		region.extent.height = PrefiltermapSize >> level;
		region.extent.depth = 1;
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = 6;
		region.srcSubresource.mipLevel = level;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.layerCount = 6;
		region.dstSubresource.mipLevel = level;
		regions.push_back(region);
	}

	for (size_t i = 0; i < probes.size(); i++)
	{
		cmdbuffer->copyImage(probes[i]->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Prefiltermaps[i].Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)regions.size(), regions.data());
	}

	VkImageTransition barrier1;
	for (size_t i = 0; i < probes.size(); i++)
		barrier1.AddImage(&Prefiltermaps[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, 0, miplevels, 0, 6);
	barrier1.Execute(cmdbuffer);
}

void VkTextureManager::CheckIrradiancemapSize(int cubeCount)
{
	int createStart = Irradiancemaps.size();
	if (Irradiancemaps.size() <= (size_t)cubeCount)
		Irradiancemaps.resize(cubeCount);

	for (int i = createStart; i < cubeCount; i++)
	{
		Irradiancemaps[i].Image = ImageBuilder()
			.Size(IrradiancemapSize, IrradiancemapSize, 1, 6)
			.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
			.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
			.Flags(VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
			.DebugName("VkTextureManager.Irradiancemap")
			.Create(fb->GetDevice());

		Irradiancemaps[i].View = ImageViewBuilder()
			.Type(VK_IMAGE_VIEW_TYPE_CUBE)
			.Image(Irradiancemaps[i].Image.get(), VK_FORMAT_R16G16B16A16_SFLOAT)
			.DebugName("VkTextureManager.IrradiancemapView")
			.Create(fb->GetDevice());
	}
}

void VkTextureManager::CheckPrefiltermapSize(int cubeCount)
{
	int createStart = Prefiltermaps.size();
	if (Prefiltermaps.size() <= (size_t)cubeCount)
		Prefiltermaps.resize(cubeCount);

	int w = PrefiltermapSize;
	int h = PrefiltermapSize;
	int pixelsize = 8;
	int miplevels = MAX_REFLECTION_LOD + 1;

	for (int i = createStart; i < cubeCount; i++)
	{
		Prefiltermaps[i].Image = ImageBuilder()
			.Size(w, h, miplevels, 6)
			.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
			.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
			.Flags(VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
			.DebugName("VkTextureManager.Prefiltermap")
			.Create(fb->GetDevice());

		Prefiltermaps[i].View = ImageViewBuilder()
			.Type(VK_IMAGE_VIEW_TYPE_CUBE)
			.Image(Prefiltermaps[i].Image.get(), VK_FORMAT_R16G16B16A16_SFLOAT)
			.DebugName("VkTextureManager.PrefiltermapView")
			.Create(fb->GetDevice());
	}
}

void VkTextureManager::UploadIrradiancemap(int cubeCount, const TArray<uint16_t>& srcPixels)
{
	CheckIrradiancemapSize(cubeCount);

	int w = IrradiancemapSize;
	int h = IrradiancemapSize;
	int pixelsize = 8;

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	if (srcPixels.size() != 0)
	{
		if (srcPixels.size() != w * h * 3 * 6 * cubeCount)
			I_FatalError("Invalid pixels array passed to VkTextureManager.CreateIrradiancemap");

		int totalSize = w * h * pixelsize * 6 * cubeCount;
		auto stagingBuffer = BufferBuilder()
			.Size(totalSize)
			.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
			.DebugName("VkTextureManager.CubeTextureListStagingBuffer")
			.Create(fb->GetDevice());

		uint16_t one = 0x3c00; // half-float 1.0
		const uint16_t* src = srcPixels.data();
		uint16_t* data = (uint16_t*)stagingBuffer->Map(0, totalSize);
		for (int i = w * h * 6 * cubeCount; i > 0; i--)
		{
			*(data++) = *(src++);
			*(data++) = *(src++);
			*(data++) = *(src++);
			*(data++) = one;
		}
		stagingBuffer->Unmap();

		VkImageTransition barrier0;
		for (int i = 0; i < cubeCount; i++)
			barrier0.AddImage(&Irradiancemaps[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true, 0, 1, 0, 6);
		barrier0.Execute(cmdbuffer);

		for (int i = 0; i < cubeCount; i++)
		{
			VkBufferImageCopy region = {};
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = 6;
			region.imageExtent.depth = 1;
			region.imageExtent.width = w;
			region.imageExtent.height = h;
			region.bufferOffset = w * h * pixelsize * 6 * i;
			cmdbuffer->copyBufferToImage(stagingBuffer->buffer, Irradiancemaps[i].Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		}

		fb->GetCommands()->TransferDeleteList->Add(std::move(stagingBuffer));
	}

	VkImageTransition barrier1;
	for (int i = 0; i < cubeCount; i++)
		barrier1.AddImage(&Irradiancemaps[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, 0, 1, 0, 6);
	barrier1.Execute(cmdbuffer);
}

void VkTextureManager::UploadPrefiltermap(int cubeCount, const TArray<uint16_t>& srcPixels)
{
	CheckPrefiltermapSize(cubeCount);

	int w = PrefiltermapSize;
	int h = PrefiltermapSize;
	int pixelsize = 8;
	int miplevels = MAX_REFLECTION_LOD + 1;

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	if (srcPixels.size() != 0)
	{
		int totalSize = 0;
		for (int level = 0; level < miplevels; level++)
		{
			int mipwidth = std::max(w >> level, 1);
			int mipheight = std::max(h >> level, 1);
			totalSize += mipwidth * mipheight * 6 * cubeCount;
		}

		if (srcPixels.size() != totalSize * 3)
			I_FatalError("Invalid pixels array passed to VkTextureManager.CreatePrefiltermap");

		auto stagingBuffer = BufferBuilder()
			.Size(totalSize * pixelsize)
			.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
			.DebugName("VkTextureManager.CreatePrefiltermap")
			.Create(fb->GetDevice());

		uint16_t one = 0x3c00; // half-float 1.0
		const uint16_t* src = srcPixels.data();
		uint16_t* data = (uint16_t*)stagingBuffer->Map(0, totalSize);
		for (int i = 0; i < totalSize; i++)
		{
			*(data++) = *(src++);
			*(data++) = *(src++);
			*(data++) = *(src++);
			*(data++) = one;
		}
		stagingBuffer->Unmap();

		VkImageTransition barrier0;
		for (int i = 0; i < cubeCount; i++)
			barrier0.AddImage(&Prefiltermaps[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true, 0, miplevels, 0, 6);
		barrier0.Execute(cmdbuffer);

		int offset = 0;
		for (int i = 0; i < cubeCount; i++)
		{
			for (int side = 0; side < 6; side++)
			{
				for (int level = 0; level < miplevels; level++)
				{
					int mipwidth = std::max(w >> level, 1);
					int mipheight = std::max(h >> level, 1);

					VkBufferImageCopy region = {};
					region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					region.imageSubresource.baseArrayLayer = side;
					region.imageSubresource.layerCount = 1;
					region.imageSubresource.mipLevel = level;
					region.imageExtent.depth = 1;
					region.imageExtent.width = mipwidth;
					region.imageExtent.height = mipheight;
					region.bufferOffset = offset;
					cmdbuffer->copyBufferToImage(stagingBuffer->buffer, Prefiltermaps[i].Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

					offset += mipwidth * mipheight * pixelsize;
				}
			}
		}

		fb->GetCommands()->TransferDeleteList->Add(std::move(stagingBuffer));
	}

	VkImageTransition barrier1;
	for (int i = 0; i < cubeCount; i++)
		barrier1.AddImage(&Prefiltermaps[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, 0, miplevels, 0, 6);
	barrier1.Execute(cmdbuffer);
}

void VkTextureManager::DownloadIrradiancemap(int probeCount, TArrayView<uint16_t> databuffer)
{
#if 0
	const int texelCount = DFrameBuffer::irrandiaceMapTexelCount;

	if (databuffer.Size() < texelCount)
		return;

	auto staging = BufferBuilder()
		.Size(32 * 32 * 8 * 6)
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU)
		.Create(fb->GetDevice());

	PipelineBarrier barrier1;
	for (int i = 0; i < 6; i++)
		barrier1.AddImage(irradianceMap.images[i].get(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	barrier1.Execute(cmdbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	for (int i = 0; i < 6; i++)
	{
		VkBufferImageCopy region = { };
		region.bufferOffset = 32 * 32 * 8 * i;
		region.imageExtent.width = irradianceMap.images[i]->width;
		region.imageExtent.height = irradianceMap.images[i]->height;
		region.imageExtent.depth = 1;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		cmdbuffer->copyImageToBuffer(irradianceMap.images[i]->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->buffer, 1, &region);
	}

	fb->GetCommands()->WaitForCommands(false);

	// Copy while dropping the alpha channel
	auto dst = databuffer.Data();
	auto src = (uint16_t*)staging->Map(0, texelCount * 8);
	for (int i = 0; i < texelCount; i++)
	{
		*(dst++) = *(src++);
		*(dst++) = *(src++);
		*(dst++) = *(src++);
		src++;
	}
	staging->Unmap();
#endif
}

void VkTextureManager::DownloadPrefiltermap(int probeCount, TArrayView<uint16_t> databuffer)
{
#if 0
	const int texelCount = DFrameBuffer::prefilterMapTexelCount;

	if (databuffer.Size() < texelCount)
		return;

	auto staging = BufferBuilder()
		.Size(prefilterMap.levelsSize * 6 * 8)
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU)
		.Create(fb->GetDevice());

	PipelineBarrier barrier1;
	for (int i = 0; i < 6 * prefilterMap.maxlevels; i++)
		barrier1.AddImage(prefilterMap.images[i].get(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	barrier1.Execute(cmdbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	int offset = 0;
	for (int i = 0; i < 6 * prefilterMap.maxlevels; i++)
	{
		VkBufferImageCopy region = { };
		region.bufferOffset = offset;
		region.imageExtent.width = prefilterMap.images[i]->width;
		region.imageExtent.height = prefilterMap.images[i]->height;
		region.imageExtent.depth = 1;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		cmdbuffer->copyImageToBuffer(prefilterMap.images[i]->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->buffer, 1, &region);

		offset += prefilterMap.images[i]->width * prefilterMap.images[i]->height * 8;
	}

	fb->GetCommands()->WaitForCommands(false);

	// Copy while dropping the alpha channel
	auto dst = databuffer.Data();
	auto src = (uint16_t*)staging->Map(0, texelCount * 8);
	for (int i = 0; i < texelCount; i++)
	{
		*(dst++) = *(src++);
		*(dst++) = *(src++);
		*(dst++) = *(src++);
		src++;
	}
	staging->Unmap();
#endif
}

void VkTextureManager::SetLightmapCount(int size, int count)
{
	int startIndex = Lightmaps.size();
	if (startIndex >= count)
		return;

	Lightmaps.resize(count);

	for (int i = startIndex; i < count; i++)
	{
		Lightmaps[i].Light.Image = ImageBuilder()
			.Size(size, size)
			.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
			.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			.DebugName("VkTextureManager.Lightmap")
			.Create(fb->GetDevice());

		Lightmaps[i].Light.View = ImageViewBuilder()
			.Type(VK_IMAGE_VIEW_TYPE_2D)
			.Image(Lightmaps[i].Light.Image.get(), VK_FORMAT_R16G16B16A16_SFLOAT)
			.DebugName("VkTextureManager.LightmapView")
			.Create(fb->GetDevice());

		Lightmaps[i].Probe.Image = ImageBuilder()
			.Size(size, size)
			.Format(VK_FORMAT_R16_UINT)
			.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			.DebugName("VkTextureManager.Probemap")
			.Create(fb->GetDevice());

		Lightmaps[i].Probe.View = ImageViewBuilder()
			.Type(VK_IMAGE_VIEW_TYPE_2D)
			.Image(Lightmaps[i].Probe.Image.get(), VK_FORMAT_R16_UINT)
			.DebugName("VkTextureManager.ProbemapView")
			.Create(fb->GetDevice());
	}

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	VkImageTransition barrier;
	for (int i = startIndex; i < count; i++)
	{
		barrier.AddImage(&Lightmaps[i].Light, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);
		barrier.AddImage(&Lightmaps[i].Probe, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);
	}
	barrier.Execute(cmdbuffer);
}

void VkTextureManager::CreateLightmap(int size, int count, const TArray<uint16_t>& srcPixels)
{
	for (auto& tex : Lightmaps)
	{
		tex.Light.Reset(fb);
		tex.Probe.Reset(fb);
	}
	Lightmaps.clear();
	Lightmaps.resize(count);

	int w = size;
	int h = size;
	int pixelsize = 8;

	for (int i = 0; i < count; i++)
	{
		Lightmaps[i].Light.Image = ImageBuilder()
			.Size(w, h)
			.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
			.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			.DebugName("VkTextureManager.Lightmap")
			.Create(fb->GetDevice());

		Lightmaps[i].Light.View = ImageViewBuilder()
			.Type(VK_IMAGE_VIEW_TYPE_2D)
			.Image(Lightmaps[i].Light.Image.get(), VK_FORMAT_R16G16B16A16_SFLOAT)
			.DebugName("VkTextureManager.LightmapView")
			.Create(fb->GetDevice());

		Lightmaps[i].Probe.Image = ImageBuilder()
			.Size(w, h)
			.Format(VK_FORMAT_R16_UINT)
			.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			.DebugName("VkTextureManager.Probemap")
			.Create(fb->GetDevice());

		Lightmaps[i].Probe.View = ImageViewBuilder()
			.Type(VK_IMAGE_VIEW_TYPE_2D)
			.Image(Lightmaps[i].Probe.Image.get(), VK_FORMAT_R16_UINT)
			.DebugName("VkTextureManager.ProbemapView")
			.Create(fb->GetDevice());
	}

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	if (srcPixels.size() != 0)
	{
		if (srcPixels.size() != w * h * 3 * count)
			I_FatalError("Invalid pixels array passed to VkTextureManager.CreateLightmap");

		int totalSize = w * h * pixelsize * count;
		auto stagingBuffer = BufferBuilder()
			.Size(totalSize)
			.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
			.DebugName("VkTextureManager.TextureListStagingBuffer")
			.Create(fb->GetDevice());

		uint16_t one = 0x3c00; // half-float 1.0
		const uint16_t* src = srcPixels.data();
		uint16_t* data = (uint16_t*)stagingBuffer->Map(0, totalSize);
		for (int i = w * h * count; i > 0; i--)
		{
			*(data++) = *(src++);
			*(data++) = *(src++);
			*(data++) = *(src++);
			*(data++) = one;
		}
		stagingBuffer->Unmap();

		VkImageTransition barrier0;
		for (int i = 0; i < count; i++)
		{
			barrier0.AddImage(&Lightmaps[i].Light, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
			barrier0.AddImage(&Lightmaps[i].Probe, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
		}
		barrier0.Execute(cmdbuffer);

		for (int i = 0; i < count; i++)
		{
			VkBufferImageCopy region = {};
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = 1;
			region.imageExtent.depth = 1;
			region.imageExtent.width = w;
			region.imageExtent.height = h;
			region.bufferOffset = w * h * pixelsize * i;
			cmdbuffer->copyBufferToImage(stagingBuffer->buffer, Lightmaps[i].Light.Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		}

		fb->GetCommands()->TransferDeleteList->Add(std::move(stagingBuffer));
	}

	VkImageTransition barrier1;
	for (int i = 0; i < count; i++)
	{
		barrier1.AddImage(&Lightmaps[i].Light, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);
		barrier1.AddImage(&Lightmaps[i].Probe, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);
	}
	barrier1.Execute(cmdbuffer);
}

void VkTextureManager::DownloadLightmap(int arrayIndex, uint16_t* buffer)
{
	DownloadTexture(&Lightmaps[arrayIndex].Light, buffer);
}

void VkTextureManager::DownloadTexture(VkTextureImage* texture, uint16_t* buffer)
{
	int width = texture->Image->width;
	int height = texture->Image->height;
	int totalSize = width * height * 4;

	auto stagingBuffer = BufferBuilder()
		.Size(totalSize * sizeof(uint16_t))
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.DebugName("DownloadLightmap")
		.Create(fb->GetDevice());

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	VkImageTransition()
		.AddImage(texture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, false)
		.Execute(cmdbuffer);

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.mipLevel = 0;
	region.imageExtent.width = width;
	region.imageExtent.height = height;
	region.imageExtent.depth = 1;
	cmdbuffer->copyImageToBuffer(texture->Image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer->buffer, 1, &region);

	VkImageTransition()
		.AddImage(texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false)
		.Execute(cmdbuffer);

	fb->GetCommands()->WaitForCommands(false);

	uint16_t* srcdata = (uint16_t*)stagingBuffer->Map(0, totalSize * sizeof(uint16_t));
	memcpy(buffer, srcdata, totalSize * sizeof(uint16_t));
	stagingBuffer->Unmap();
}

int VkTextureManager::CreateUploadID(VkHardwareTexture* tex)
{
	int id = NextUploadID++;
	PendingUploads[id] = tex;
	return id;
}

bool VkTextureManager::CheckUploadID(int id)
{
	auto it = PendingUploads.find(id);
	if (it == PendingUploads.end())
		return false;
	PendingUploads.erase(it);
	return true;
}

void VkTextureManager::RunOnWorkerThread(std::function<void()> task)
{
	std::unique_lock lock(Worker.Mutex);
	Worker.WorkerTasks.push_back(std::move(task));
	lock.unlock();
	Worker.CondVar.notify_one();
}

void VkTextureManager::RunOnMainThread(std::function<void()> task)
{
	std::unique_lock lock(Worker.Mutex);
	Worker.MainTasks.push_back(std::move(task));
}

void VkTextureManager::StartWorkerThread()
{
	Worker.Thread = std::thread([this]() { WorkerThreadMain(); });
}

void VkTextureManager::StopWorkerThread()
{
	std::unique_lock lock(Worker.Mutex);
	Worker.StopFlag = true;
	lock.unlock();
	Worker.CondVar.notify_all();
	Worker.Thread.join();
	lock.lock();
	Worker.WorkerTasks.clear();
	Worker.MainTasks.clear();
	Worker.StopFlag = false;
}

void VkTextureManager::ProcessMainThreadTasks()
{
	std::unique_lock lock(Worker.Mutex);
	std::vector<std::function<void()>> tasks;
	tasks.swap(Worker.MainTasks);
	lock.unlock();

	for (auto& task : tasks)
	{
		task();
	}
}

void VkTextureManager::WorkerThreadMain()
{
	std::unique_lock lock(Worker.Mutex);
	while (true)
	{
		Worker.CondVar.wait(lock, [&] { return Worker.StopFlag || !Worker.WorkerTasks.empty(); });
		if (Worker.StopFlag)
			break;

		std::function<void()> task;

		if (!Worker.WorkerTasks.empty())
		{
			task = std::move(Worker.WorkerTasks.front());
			Worker.WorkerTasks.pop_front();
		}

		if (task)
		{
			lock.unlock();

			try
			{
				task();
			}
			catch (...)
			{
				auto exception = std::current_exception();
				RunOnMainThread([=]() { std::rethrow_exception(exception); });
			}

			lock.lock();
		}
	}
}
