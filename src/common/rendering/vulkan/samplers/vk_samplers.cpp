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
#include <zvulkan/vulkandevice.h>
#include <zvulkan/vulkanbuilders.h>
#include "c_cvars.h"
#include "v_video.h"
#include "hw_cvars.h"
#include "vulkan/vk_renderdevice.h"
#include "vulkan/commands/vk_commandbuffer.h"
#include "vk_samplers.h"
#include "hw_material.h"
#include "i_interface.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"
#include <unordered_set>

struct VkTexFilter
{
	VkFilter minFilter;
	VkFilter magFilter;
	VkSamplerMipmapMode mipfilter;
	bool mipmapping;
};

static VkTexFilter TexFilter[] =
{
	{VK_FILTER_NEAREST,		VK_FILTER_NEAREST,		VK_SAMPLER_MIPMAP_MODE_NEAREST, false},
	{VK_FILTER_NEAREST,		VK_FILTER_NEAREST,		VK_SAMPLER_MIPMAP_MODE_NEAREST, true},
	{VK_FILTER_LINEAR,		VK_FILTER_LINEAR,		VK_SAMPLER_MIPMAP_MODE_NEAREST, false},
	{VK_FILTER_LINEAR,		VK_FILTER_LINEAR,		VK_SAMPLER_MIPMAP_MODE_NEAREST, true},
	{VK_FILTER_LINEAR,		VK_FILTER_LINEAR,		VK_SAMPLER_MIPMAP_MODE_LINEAR, true},
	{VK_FILTER_NEAREST,		VK_FILTER_NEAREST,		VK_SAMPLER_MIPMAP_MODE_LINEAR, true},
	{VK_FILTER_LINEAR,		VK_FILTER_NEAREST,		VK_SAMPLER_MIPMAP_MODE_LINEAR, true}
};

struct VkTexClamp
{
	VkSamplerAddressMode clamp_u;
	VkSamplerAddressMode clamp_v;
};

static VkTexClamp TexClamp[] =
{
	{ VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT },
	{ VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_REPEAT },
	{ VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE },
	{ VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE },
};

std::unordered_set<uint32_t> CurrentDriverIntelDeviceIDs =
{
	// Devices out now that may have an older driver still
	0x0BDA, 0x0BD9, 0x0BDB, 0x0BD7, 0x0BD6, 0x0BD0, 0x0BD5, 0x56C0,
	0x56C1, 0x56B3, 0x56B2, 0x56A4, 0x56A3, 0x5697, 0x5696, 0x5695,
	0x56B1, 0x56B0, 0x56A6, 0x56A5, 0x56A1, 0x56A0, 0x5694, 0x5693,
	0x5692, 0x5691, 0x5690, 0xA7A9, 0xA7A8, 0xA7A1, 0xA7A0, 0xA721,
	0xA720, 0xA78B, 0xA78A, 0xA789, 0xA788, 0xA783, 0xA782, 0xA781,
	0xA780, 0x4907, 0x4905, 0x4680, 0x4682, 0x4688, 0x468A, 0x468B,
	0x4690, 0x4692, 0x4693, 0x46D0, 0x46D1, 0x46D2, 0x4626, 0x4628,
	0x462A, 0x46A0, 0x46A1, 0x46A2, 0x46A3, 0x46A6, 0x46A8, 0x46AA,
	0x46B0, 0x46B1, 0x46B2, 0x46B3, 0x46C0, 0x46C1, 0x46C2, 0x46C3,
	0x4C8A, 0x4C8B, 0x4C90, 0x4C9A, 0x4C8C, 0x4C80, 0x4E71, 0x4E61,
	0x4E57, 0x4E55, 0x4E51, 0x4571, 0x4557, 0x4555, 0x4551, 0x4541,
	0x9A59, 0x9A60, 0x9A68, 0x9A70, 0x9A40, 0x9A49, 0x9A78, 0x9AC0,
	0x9AC9, 0x9AD9, 0x9AF8
};

std::unordered_set<uint32_t> LegacyIntelDeviceIDs =
{
	// Not under active development as of 2024
	0x8A70, 0x8A71, 0x8A56, 0x8A58, 0x8A5B, 0x8A5D, 0x8A54, 0x8A5A,
	0x8A5C, 0x8A57, 0x8A59, 0x8A50, 0x8A51, 0x8A52, 0x8A53, 0x3EA5,
	0x3EA8, 0x3EA6, 0x3EA7, 0x3EA2, 0x3E90, 0x3E93, 0x3E99, 0x3E9C,
	0x3EA1, 0x9BA5, 0x9BA8, 0x3EA4, 0x9B21, 0x9BA0, 0x9BA2, 0x9BA4,
	0x9BAA, 0x9BAB, 0x9BAC, 0x87CA, 0x3EA3, 0x9B41, 0x9BC0, 0x9BC2,
	0x9BC4, 0x9BCA, 0x9BCB, 0x9BCC, 0x3E91, 0x3E92, 0x3E98, 0x3E9B,
	0x9BC5, 0x9BC8, 0x3E96, 0x3E9A, 0x3E94, 0x9BC6, 0x9BE6, 0x9BF6,
	0x3EA9, 0x3EA0, 0x593B, 0x5923, 0x5926, 0x5927, 0x5917, 0x5912,
	0x591B, 0x5916, 0x5921, 0x591A, 0x591D, 0x591E, 0x591C, 0x87C0,
	0x5913, 0x5915, 0x5902, 0x5906, 0x590B, 0x590A, 0x5908, 0x590E,
	0x3185, 0x3184, 0x1A85, 0x5A85, 0x0A84, 0x1A84, 0x5A84, 0x192A,
	0x1932, 0x193B, 0x193A, 0x193D, 0x1923, 0x1926, 0x1927, 0x192B,
	0x192D, 0x1912, 0x191B, 0x1913, 0x1915, 0x1917, 0x191A, 0x1916,
	0x1921, 0x191D, 0x191E, 0x1902, 0x1906, 0x190B, 0x190A, 0x190E,

	// Hardware completely out of support. Generally only seen in Linux now:
	0x163D, 0x163A, 0x1632, 0x163E, 0x163B, 0x1636, 0x1622, 0x1626,
	0x162A, 0x162B, 0x162D, 0x162E, 0x1612, 0x1616, 0x161A, 0x161B,
	0x161D, 0x161E, 0x1602, 0x1606, 0x160A, 0x160B, 0x160D, 0x160E,
	0x22B0, 0x22B2, 0x22B3, 0x22B1, 0x0F30, 0x0F31, 0x0F32, 0x0F33,
	0x0157, 0x0155, 0x0422, 0x0426, 0x042A, 0x042B, 0x042E, 0x0C22,
	0x0C26, 0x0C2A, 0x0C2B, 0x0C2E, 0x0A22, 0x0A2A, 0x0A2B, 0x0D2A,
	0x0D2B, 0x0D2E, 0x0A26, 0x0A2E, 0x0D22, 0x0D26, 0x0412, 0x0416,
	0x0D12, 0x041A, 0x041B, 0x0C12, 0x0C16, 0x0C1A, 0x0C1B, 0x0C1E,
	0x0A12, 0x0A1A, 0x0A1B, 0x0D16, 0x0D1A, 0x0D1B, 0x0D1E, 0x041E,
	0x0A16, 0x0A1E, 0x0402, 0x0406, 0x040A, 0x040B, 0x040E, 0x0C02,
	0x0C06, 0x0C0A, 0x0C0B, 0x0C0E, 0x0A02, 0x0A06, 0x0A0A, 0x0A0B,
	0x0A0E, 0x0D02, 0x0D06, 0x0D0A, 0x0D0B, 0x0D0E, 0x0162, 0x0166,
	0x016A, 0x0152, 0x0156, 0x015A, 0x0112, 0x0122, 0x0116, 0x0126,
	0x0102, 0x0106, 0x010A, 0x0042, 0x0046, 0x2A42, 0x2E02, 0x2E12,
	0x2E22, 0x2E32, 0x2E42, 0x2E92, 0x29A2, 0x2982, 0x2992, 0x2972,
	0x2A02, 0x2A12, 0xA001, 0xA011, 0x29D2, 0x29C2, 0x29B2, 0x27AE,
	0x27A2, 0x2772, 0x2592, 0x258A, 0x2582, 0x2572, 0x3582, 0x358E,
	0x2562, 0x3577
};

VkSamplerManager::VkSamplerManager(VulkanRenderDevice* fb) : fb(fb)
{
	CreateHWSamplers();
	CreateShadowmapSampler();
	CreateLightmapSampler();
}

VkSamplerManager::~VkSamplerManager()
{
}

void VkSamplerManager::ResetHWSamplers()
{
	DeleteHWSamplers();
	CreateHWSamplers();
}

void VkSamplerManager::CreateHWSamplers()
{
	int filter = sysCallbacks.DisableTextureFilter && sysCallbacks.DisableTextureFilter() ? 0 : gl_texture_filter;

	const auto& properties = fb->GetDevice()->PhysicalDevice.Properties.Properties;

	// Anisotropy puts extra load on the memory architecture.
	// Unless the user explicitly chose their own filter value, default to lower filtering for integrated GPUs as they are generally slow.
	float maxAnisotropy = gl_texture_filter_anisotropic;
	if (maxAnisotropy < 1.0f)
	{
		if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			maxAnisotropy = 4.0f;
		else
			maxAnisotropy = 8.0f;
	}

	// Intel devices handles anisotropic filtering differently than what we want.
	bool brokenNearestMipLinear = false;
	if (properties.vendorID == 0x8086)
	{
		bool isLegacyDevice = LegacyIntelDeviceIDs.find(properties.deviceID) != LegacyIntelDeviceIDs.end();
		bool isCurrentDriverDevice = CurrentDriverIntelDeviceIDs.find(properties.deviceID) != CurrentDriverIntelDeviceIDs.end();
		if (isLegacyDevice ||
			(isCurrentDriverDevice && VK_VERSION_MAJOR(properties.driverVersion) == 0 && VK_VERSION_MINOR(properties.driverVersion) < 405) ||
			(isCurrentDriverDevice && VK_VERSION_MAJOR(properties.driverVersion) == 0 && VK_VERSION_MINOR(properties.driverVersion) == 405 && VK_VERSION_PATCH(properties.driverVersion) < 1286))
		{
			// Old hardware and old drivers have to turn AF off if we want any nearest filtering.
			if (TexFilter[filter].magFilter != VK_FILTER_LINEAR || TexFilter[filter].minFilter != VK_FILTER_LINEAR)
			{
				maxAnisotropy = 1.0f;
			}
		}
		else
		{
			// None (trilinear) still doesn't work. Use None (linear mipmap) instead
			if (filter == 6)
			{
				filter = 5;
			}
			brokenNearestMipLinear = true;
		}
	}

	for (int i = CLAMP_NONE; i <= CLAMP_XY; i++)
	{
		SamplerBuilder builder;
		builder.MagFilter(TexFilter[filter].magFilter);
		builder.MinFilter(TexFilter[filter].minFilter);
		builder.AddressMode(TexClamp[i].clamp_u, TexClamp[i].clamp_v, VK_SAMPLER_ADDRESS_MODE_REPEAT);
		builder.MipmapMode(TexFilter[filter].mipfilter);
		if (TexFilter[filter].mipmapping)
		{
			if (maxAnisotropy > 1.0f)
				builder.Anisotropy(maxAnisotropy);
			builder.MaxLod(100.0f); // According to the spec this value is clamped so something high makes it usable for all textures.
		}
		else
		{
			builder.MaxLod(0.25f);
		}
		builder.DebugName("VkSamplerManager.mSamplers");
		mSamplers[i] = builder.Create(fb->GetDevice());
	}

	mSamplers[CLAMP_XY_NOMIP] = SamplerBuilder()
		.MagFilter(TexFilter[filter].magFilter)
		.MinFilter(TexFilter[filter].magFilter)
		.AddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_REPEAT)
		.MipmapMode(VK_SAMPLER_MIPMAP_MODE_NEAREST)
		.MaxLod(0.25f)
		.DebugName("VkSamplerManager.mSamplers")
		.Create(fb->GetDevice());

	for (int i = CLAMP_NOFILTER; i <= CLAMP_NOFILTER_XY; i++)
	{
		mSamplers[i] = SamplerBuilder()
			.MagFilter(VK_FILTER_NEAREST)
			.MinFilter(VK_FILTER_NEAREST)
			.AddressMode(TexClamp[i - CLAMP_NOFILTER].clamp_u, TexClamp[i - CLAMP_NOFILTER].clamp_v, VK_SAMPLER_ADDRESS_MODE_REPEAT)
			.MipmapMode(VK_SAMPLER_MIPMAP_MODE_NEAREST)
			.MaxLod(0.25f)
			.DebugName("VkSamplerManager.mSamplers")
			.Create(fb->GetDevice());
	}

	// CAMTEX is repeating with texture filter and no mipmap
	mSamplers[CLAMP_CAMTEX] = SamplerBuilder()
		.MagFilter(TexFilter[filter].magFilter)
		.MinFilter(TexFilter[filter].magFilter)
		.AddressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT)
		.MipmapMode(VK_SAMPLER_MIPMAP_MODE_NEAREST)
		.MaxLod(0.25f)
		.DebugName("VkSamplerManager.mSamplers")
		.Create(fb->GetDevice());

	{
		SamplerBuilder builder;
		builder.MagFilter(VK_FILTER_NEAREST);
		builder.MinFilter(!brokenNearestMipLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
		builder.AddressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT);
		builder.MipmapMode(VK_SAMPLER_MIPMAP_MODE_LINEAR);
		builder.MaxLod(100.0f);
		if (maxAnisotropy > 1.0f)
			builder.Anisotropy(maxAnisotropy);
		mOverrideSamplers[int(MaterialLayerSampling::NearestMipLinear)] = builder.Create(fb->GetDevice());
	}

	{
		SamplerBuilder builder;
		builder.MagFilter(VK_FILTER_LINEAR);
		builder.MinFilter(VK_FILTER_LINEAR);
		builder.AddressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT);
		builder.MipmapMode(VK_SAMPLER_MIPMAP_MODE_LINEAR);
		builder.MaxLod(100.0f);
		if (maxAnisotropy > 1.0f)
			builder.Anisotropy(maxAnisotropy);
		mOverrideSamplers[int(MaterialLayerSampling::LinearMipLinear)] = builder.Create(fb->GetDevice());
	}
}

void VkSamplerManager::DeleteHWSamplers()
{
	auto deleteSamplers = [&](auto& samplers)
	{
		for (auto& sampler : samplers)
		{
			if (sampler)
				fb->GetCommands()->DrawDeleteList->Add(std::move(sampler));
		}
	};

	deleteSamplers(mSamplers);
	deleteSamplers(mOverrideSamplers);
}

VulkanSampler* VkSamplerManager::Get(PPFilterMode filter, PPWrapMode wrap)
{
	int index = (((int)filter) << 1) | (int)wrap;
	auto& sampler = mPPSamplers[index];
	if (sampler)
		return sampler.get();

	sampler = SamplerBuilder()
		.MipmapMode(VK_SAMPLER_MIPMAP_MODE_NEAREST)
		.MinFilter(filter == PPFilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR)
		.MagFilter(filter == PPFilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR)
		.AddressMode(wrap == PPWrapMode::Clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT)
		.DebugName("VkPostprocess.mSamplers")
		.Create(fb->GetDevice());

	return sampler.get();
}

void VkSamplerManager::CreateShadowmapSampler()
{
	ShadowmapSampler = SamplerBuilder()
		.MipmapMode(VK_SAMPLER_MIPMAP_MODE_NEAREST)
		.MinFilter(VK_FILTER_NEAREST)
		.MagFilter(VK_FILTER_NEAREST)
		.AddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
		.DebugName("VkRenderBuffers.ShadowmapSampler")
		.Create(fb->GetDevice());
}

void VkSamplerManager::CreateLightmapSampler()
{
	LightmapSampler = SamplerBuilder()
		.MipmapMode(VK_SAMPLER_MIPMAP_MODE_LINEAR)
		.MinFilter(VK_FILTER_LINEAR)
		.MagFilter(VK_FILTER_LINEAR)
		.AddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
		.DebugName("VkRenderBuffers.LightmapSampler")
		.Create(fb->GetDevice());
}
