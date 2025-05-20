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

void VkTextureManager::BeginFrame()
{
	if (!Shadowmap.Image || Shadowmap.Image->width != gl_shadowmap_quality)
	{
		Shadowmap.Reset(fb);
		CreateShadowmap();
	}
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
		I_FatalError("Unexpected file size for '%s'");

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
	TArray<uint16_t> data;
	for (int arrayIndex = 0; arrayIndex < 6; arrayIndex++)
	{
		data.Push(0);
		data.Push(0);
		data.Push(0);
	}
	CreateIrradiancemap(1, 6, std::move(data));
}

void VkTextureManager::CreatePrefiltermap()
{
	TArray<uint16_t> data;
	int size = 1 << MAX_REFLECTION_LOD;
	for (int arrayIndex = 0; arrayIndex < 6; arrayIndex++)
	{
		for (int level = 0; level <= MAX_REFLECTION_LOD; level++)
		{
			int mipsize = size >> level;
			for (int i = 0; i < mipsize * mipsize; i++)
			{
				data.Push(0);
				data.Push(0);
				data.Push(0);
			}
		}
	}
	CreatePrefiltermap(size, 6, std::move(data));
}

void VkTextureManager::CreateIrradiancemap(int size, int count, const TArray<uint16_t>& newPixelData)
{
	if (Irradiancemap.Size == size && Irradiancemap.Count == count && newPixelData.Size() == 0)
		return;

	Irradiancemap.Size = size;
	Irradiancemap.Count = count;

	int w = size;
	int h = size;
	int pixelsize = 8;

	Irradiancemap.Image.Reset(fb);

	Irradiancemap.Image.Image = ImageBuilder()
		.Size(w, h, 1, count)
		.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
		.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.Flags(VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
		.DebugName("VkTextureManager.Irradiancemap")
		.Create(fb->GetDevice());

	Irradiancemap.Image.View = ImageViewBuilder()
		.Type(VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
		.Image(Irradiancemap.Image.Image.get(), VK_FORMAT_R16G16B16A16_SFLOAT)
		.DebugName("VkTextureManager.IrradiancemapView")
		.Create(fb->GetDevice());

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	assert(newPixelData.Size() == (size_t)w * h * count * 3);

	if (count > 0 && newPixelData.Size() == (size_t)w * h * count * 3)
	{
		int totalSize = w * h * count * pixelsize;

		auto stagingBuffer = BufferBuilder()
			.Size(totalSize)
			.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
			.DebugName("VkRenderBuffers.IrradiancemapStagingBuffer")
			.Create(fb->GetDevice());

		uint16_t one = 0x3c00; // half-float 1.0
		const uint16_t* src = newPixelData.Data();
		uint16_t* data = (uint16_t*)stagingBuffer->Map(0, totalSize);
		for (int i = w * h * count; i > 0; i--)
		{
			*(data++) = *(src++);
			*(data++) = *(src++);
			*(data++) = *(src++);
			*(data++) = one;
		}
		stagingBuffer->Unmap();

		VkImageTransition()
			.AddImage(&Irradiancemap.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true, 0, 1, 0, count)
			.Execute(cmdbuffer);

		VkBufferImageCopy region = {};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = count;
		region.imageExtent.depth = 1;
		region.imageExtent.width = w;
		region.imageExtent.height = h;
		cmdbuffer->copyBufferToImage(stagingBuffer->buffer, Irradiancemap.Image.Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		fb->GetCommands()->TransferDeleteList->Add(std::move(stagingBuffer));
	}

	VkImageTransition()
		.AddImage(&Irradiancemap.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, 0, 1, 0, count)
		.Execute(cmdbuffer);
}

void VkTextureManager::CreatePrefiltermap(int size, int count, const TArray<uint16_t>& newPixelData)
{
	if (Prefiltermap.Size == size && Prefiltermap.Count == count && newPixelData.Size() == 0)
		return;

	Prefiltermap.Size = size;
	Prefiltermap.Count = count;

	int w = size;
	int h = size;
	int pixelsize = 8;
	int miplevels = MAX_REFLECTION_LOD + 1;

	Prefiltermap.Image.Reset(fb);

	Prefiltermap.Image.Image = ImageBuilder()
		.Size(w, h, miplevels, count)
		.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
		.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.Flags(VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
		.DebugName("VkTextureManager.Prefiltermap")
		.Create(fb->GetDevice());

	Prefiltermap.Image.View = ImageViewBuilder()
		.Type(VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
		.Image(Prefiltermap.Image.Image.get(), VK_FORMAT_R16G16B16A16_SFLOAT)
		.DebugName("VkTextureManager.PrefiltermapView")
		.Create(fb->GetDevice());

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	int totalSize = 0;
	for (int level = 0; level < miplevels; level++)
	{
		int mipwidth = std::max(w >> level, 1);
		int mipheight = std::max(h >> level, 1);
		totalSize += mipwidth * mipheight * count;
	}
	assert(newPixelData.Size() == (size_t)totalSize * 3);

	if (count > 0 && newPixelData.Size() == (size_t)totalSize * 3)
	{
		auto stagingBuffer = BufferBuilder()
			.Size(totalSize * pixelsize)
			.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
			.DebugName("VkTextureManager.PrefiltermapStagingBuffer")
			.Create(fb->GetDevice());

		uint16_t one = 0x3c00; // half-float 1.0
		const uint16_t* src = newPixelData.Data();
		uint16_t* data = (uint16_t*)stagingBuffer->Map(0, totalSize * pixelsize);
		for (int i = 0; i < totalSize; i++)
		{
			*(data++) = *(src++);
			*(data++) = *(src++);
			*(data++) = *(src++);
			*(data++) = one;
		}
		stagingBuffer->Unmap();

		VkImageTransition()
			.AddImage(&Prefiltermap.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true, 0, miplevels, 0, count)
			.Execute(cmdbuffer);

		int offset = 0;
		for (int i = 0; i < count; ++i)
		{
			for (int level = 0; level < miplevels; level++)
			{
				int mipwidth = std::max(w >> level, 1);
				int mipheight = std::max(h >> level, 1);

				VkBufferImageCopy region = {};
				region.bufferOffset = offset;
				region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				region.imageSubresource.layerCount = 1;
				region.imageSubresource.baseArrayLayer = i;
				region.imageSubresource.mipLevel = level;
				region.imageExtent.depth = 1;
				region.imageExtent.width = mipwidth;
				region.imageExtent.height = mipheight;
				cmdbuffer->copyBufferToImage(stagingBuffer->buffer, Prefiltermap.Image.Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

				offset += mipwidth * mipheight * pixelsize;
			}
		}

		fb->GetCommands()->TransferDeleteList->Add(std::move(stagingBuffer));
	}

	VkImageTransition()
		.AddImage(&Prefiltermap.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, 0, miplevels, 0, count)
		.Execute(cmdbuffer);
}

void VkTextureManager::CreateLightmap(int newLMTextureSize, int newLMTextureCount, TArray<uint16_t>&& newPixelData)
{
	if (newLMTextureCount == 0) // If there is no lightmap we create a dummy texture to simplify code elsewhere
	{
		newLMTextureSize = 1;
		newLMTextureCount = 1;
		newPixelData = {};
	}

	if (Lightmap.Size == newLMTextureSize && Lightmap.Count == newLMTextureCount + 1 && newPixelData.Size() == 0)
		return;

	Lightmap.Size = newLMTextureSize;

	// Always create minimum 16 textures with 4 additional than the initial watermark
	// To do: add support for expanding the texture dynamically instead
	Lightmap.Count = std::max(newLMTextureCount, 12) + 4;
	
	int w = newLMTextureSize;
	int h = newLMTextureSize;
	int count = newLMTextureCount;
	int pixelsize = 8;

	Lightmap.Image.Reset(fb);

	Lightmap.Image.Image = ImageBuilder()
		.Size(w, h, 1, Lightmap.Count)
		.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
		.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		.DebugName("VkTextureManager.Lightmap")
		.Create(fb->GetDevice());

	Lightmap.Image.View = ImageViewBuilder()
		.Type(VK_IMAGE_VIEW_TYPE_2D_ARRAY)
		.Image(Lightmap.Image.Image.get(), VK_FORMAT_R16G16B16A16_SFLOAT)
		.DebugName("VkTextureManager.LightmapView")
		.Create(fb->GetDevice());

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	if (count > 0 && newPixelData.Size() >= (size_t)w * h * count * 3)
	{
		assert(newPixelData.Size() == (size_t)w * h * count * 3);

		int totalSize = w * h * count * pixelsize;

		auto stagingBuffer = BufferBuilder()
			.Size(totalSize)
			.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
			.DebugName("VkTextureManager.LightmapStagingBuffer")
			.Create(fb->GetDevice());

		uint16_t one = 0x3c00; // half-float 1.0
		const uint16_t* src = newPixelData.Data();
		uint16_t* data = (uint16_t*)stagingBuffer->Map(0, totalSize);
		for (int i = w * h * count; i > 0; i--)
		{
			*(data++) = *(src++);
			*(data++) = *(src++);
			*(data++) = *(src++);
			*(data++) = one;
		}
		stagingBuffer->Unmap();

		VkImageTransition()
			.AddImage(&Lightmap.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true, 0, 1, 0, Lightmap.Count)
			.Execute(cmdbuffer);

		VkBufferImageCopy region = {};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = count;
		region.imageExtent.depth = 1;
		region.imageExtent.width = w;
		region.imageExtent.height = h;
		cmdbuffer->copyBufferToImage(stagingBuffer->buffer, Lightmap.Image.Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		fb->GetCommands()->TransferDeleteList->Add(std::move(stagingBuffer));
	
		newPixelData.Clear();
	}

	VkImageTransition()
		.AddImage(&Lightmap.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, 0, 1, 0, Lightmap.Count)
		.Execute(cmdbuffer);
}

void VkTextureManager::DownloadLightmap(int arrayIndex, uint16_t* buffer)
{
	unsigned int totalSize = Lightmap.Size * Lightmap.Size * 4;

	auto stagingBuffer = BufferBuilder()
		.Size(totalSize * sizeof(uint16_t))
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.DebugName("DownloadLightmap")
		.Create(fb->GetDevice());

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	PipelineBarrier()
		.AddImage(Lightmap.Image.Image.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, arrayIndex, 1)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.baseArrayLayer = arrayIndex;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.mipLevel = 0;
	region.imageExtent.width = Lightmap.Size;
	region.imageExtent.height = Lightmap.Size;
	region.imageExtent.depth = 1;
	cmdbuffer->copyImageToBuffer(Lightmap.Image.Image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer->buffer, 1, &region);

	PipelineBarrier()
		.AddImage(Lightmap.Image.Image.get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, arrayIndex, 1)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

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
