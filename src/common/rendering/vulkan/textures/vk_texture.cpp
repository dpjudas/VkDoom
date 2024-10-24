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

VkTextureManager::VkTextureManager(VulkanRenderDevice* fb) : fb(fb)
{
	CreateNullTexture();
	CreateShadowmap();
	CreateLightmap();
	CreateIrradiancemap();
	CreatePrefiltermap();
}

VkTextureManager::~VkTextureManager()
{
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
		for (int level = 0; level < MAX_REFLECTION_LOD; level++)
		{
			int mipsize = size >> level;
			for (int i = 0; i < mipsize; i++)
			{
				data.Push(0);
				data.Push(0);
				data.Push(0);
			}
		}
	}
	CreatePrefiltermap(size, 6, std::move(data));
}

void VkTextureManager::CreateIrradiancemap(int size, int count, TArray<uint16_t>&& newPixelData)
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

	if (count > 0 && newPixelData.Size() >= (size_t)w * h * count * 3)
	{
		assert(newPixelData.Size() == (size_t)w * h * count * 3);

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

		newPixelData.Clear();
	}

	VkImageTransition()
		.AddImage(&Irradiancemap.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, 0, 1, 0, count)
		.Execute(cmdbuffer);
}

void VkTextureManager::CreatePrefiltermap(int size, int count, TArray<uint16_t>&& newPixelData)
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

	if (count > 0 && newPixelData.Size() >= (size_t)w * h * count * 3)
	{
		assert(newPixelData.Size() == (size_t)w * h * count * 3);

		int totalSize = 0;
		for (int level = 0; level < miplevels; level++)
		{
			int mipwidth = std::max(w >> level, 1);
			int mipheight = std::max(h >> level, 1);
			totalSize += mipwidth * mipheight * count * pixelsize;
		}

		auto stagingBuffer = BufferBuilder()
			.Size(totalSize)
			.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
			.DebugName("VkTextureManager.PrefiltermapStagingBuffer")
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
			.AddImage(&Prefiltermap.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true, 0, miplevels, 0, count)
			.Execute(cmdbuffer);

		int offset = 0;
		for (int level = 0; level < miplevels; level++)
		{
			VkBufferImageCopy region = {};
			region.bufferOffset = offset;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = count;
			region.imageSubresource.mipLevel = level;
			region.imageExtent.depth = 1;
			region.imageExtent.width = w;
			region.imageExtent.height = h;
			cmdbuffer->copyBufferToImage(stagingBuffer->buffer, Prefiltermap.Image.Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			int mipwidth = std::max(w >> level, 1);
			int mipheight = std::max(h >> level, 1);
			offset += mipwidth * mipheight * count * pixelsize;
		}

		fb->GetCommands()->TransferDeleteList->Add(std::move(stagingBuffer));

		newPixelData.Clear();
	}

	VkImageTransition()
		.AddImage(&Prefiltermap.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, 0, miplevels, 0, count)
		.Execute(cmdbuffer);
}

void VkTextureManager::CreateLightmap(int newLMTextureSize, int newLMTextureCount, TArray<uint16_t>&& newPixelData)
{
	if (Lightmap.Size == newLMTextureSize && Lightmap.Count == newLMTextureCount + 1 && newPixelData.Size() == 0)
		return;

	Lightmap.Size = newLMTextureSize;
	Lightmap.Count = newLMTextureCount + 1; // the extra texture is for the dynamic lightmap
	
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
