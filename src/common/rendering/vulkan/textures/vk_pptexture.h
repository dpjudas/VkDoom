
#pragma once

#include <zvulkan/vulkanobjects.h>
#include <list>
#include <memory>

#include "hwrenderer/postprocessing/hw_postprocess.h"
#include "vulkan/textures/vk_imagetransition.h"
#include "vulkan/vulkan_core.h"

class VulkanRenderDevice;
class VulkanBuffer;

class VkPPTexture : public PPTextureBackend
{
public:
	VkPPTexture(VulkanRenderDevice* fb, PPTexture *texture);
	~VkPPTexture();

	void Reset();

	VulkanRenderDevice* fb = nullptr;
	std::list<VkPPTexture*>::iterator it;

	VkTextureImage TexImage;
	std::unique_ptr<VulkanBuffer> Staging;
	VkFormat Format;
};
