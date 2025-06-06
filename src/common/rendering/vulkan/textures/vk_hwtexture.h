#pragma once

#ifdef LoadImage
#undef LoadImage
#endif

#define SHADED_TEXTURE -1
#define DIRECT_PALETTE -2

#include "tarray.h"
#include "hw_ihwtexture.h"
#include <zvulkan/vulkanobjects.h>
#include "vk_imagetransition.h"
#include "hw_material.h"
#include <list>

struct FMaterialState;
class VulkanDescriptorSet;
class VulkanImage;
class VulkanImageView;
class VulkanBuffer;
class VulkanRenderDevice;
class FGameTexture;

class VkHardwareTexture : public IHardwareTexture
{
	friend class VkMaterial;
public:
	VkHardwareTexture(VulkanRenderDevice* fb, int numchannels);
	~VkHardwareTexture();

	void Reset();

	// Software renderer stuff
	void AllocateBuffer(int w, int h, int texelsize) override;
	uint8_t *MapBuffer() override;
	unsigned int CreateTexture(unsigned char * buffer, int w, int h, int texunit, bool mipmap, const char *name) override;

	// Wipe screen
	void CreateWipeTexture(int w, int h, const char *name);

	VkTextureImage *GetImage(FTexture *tex, int translation, int flags);
	VkTextureImage *GetDepthStencil(FTexture *tex);

	VulkanRenderDevice* fb = nullptr;
	std::list<VkHardwareTexture*>::iterator it;

private:
	void CreateImage(VkTextureImage* image, FTexture *tex, int translation, int flags);

	void CreateTexture(VkTextureImage* image, int w, int h, int pixelsize, VkFormat format, const void *pixels, bool mipmap);
	void UploadTexture(VkTextureImage* image, int w, int h, int pixelsize, VkFormat format, const void* pixels, bool mipmap);
	static int GetMipLevels(int w, int h);

	VkTextureImage mImage, mPaletteImage;
	int mTexelsize = 4;

	VkTextureImage mDepthStencil;

	uint8_t* mappedSWFB = nullptr;
};

class VkMaterial : public FMaterial
{
public:
	VkMaterial(VulkanRenderDevice* fb, FGameTexture* tex, int scaleflags);
	~VkMaterial();

	void DeleteDescriptors() override;

	VulkanRenderDevice* fb = nullptr;
	std::list<VkMaterial*>::iterator it;

	int GetBindlessIndex(const FMaterialState& state);

private:
	struct DescriptorEntry
	{
		int clampmode;
		intptr_t remap;
		int bindlessIndex;
		GlobalShaderAddr globalShaderAddr;
		bool indexed;

		DescriptorEntry(int cm, intptr_t f, int index, GlobalShaderAddr addr, bool paletteMode)
		{
			clampmode = cm;
			remap = f;
			bindlessIndex = index;
			globalShaderAddr = addr;
			indexed = paletteMode;
		}
	};

	DescriptorEntry& GetDescriptorEntry(const FMaterialState& state);

	std::vector<DescriptorEntry> mDescriptorSets;
};
