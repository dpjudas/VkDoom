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

#include "vk_descriptorset.h"
#include "vulkan/vk_renderdevice.h"
#include "vulkan/vk_levelmesh.h"
#include "vulkan/shaders/vk_shader.h"
#include "vulkan/samplers/vk_samplers.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include "vulkan/textures/vk_hwtexture.h"
#include "vulkan/textures/vk_texture.h"
#include "vulkan/buffers/vk_hwbuffer.h"
#include "vulkan/buffers/vk_buffer.h"
#include "vulkan/buffers/vk_rsbuffers.h"
#include "vulkan/commands/vk_commandbuffer.h"
#include "vulkan/pipelines/vk_pprenderpass.h"
#include <zvulkan/vulkanbuilders.h>
#include "flatvertices.h"
#include "hw_viewpointuniforms.h"
#include "v_2ddrawer.h"
#include "fcolormap.h"

VkDescriptorSetManager::VkDescriptorSetManager(VulkanRenderDevice* fb) : fb(fb)
{
	CreateLevelMeshLayout();
	CreateRSBufferLayout();
	CreateFixedLayout();
	CreateLightTilesLayout();
	CreateZMinMaxLayout();
	CreateLevelMeshPool();
	CreateRSBufferPool();
	CreateFixedPool();
	CreateLightTilesPool();
	CreateZMinMaxPool();
	CreateBindlessSet();
}

VkDescriptorSetManager::~VkDescriptorSetManager()
{
	while (!Materials.empty())
		RemoveMaterial(Materials.back());
}

void VkDescriptorSetManager::Init()
{
	RSBuffer.Set = RSBuffer.Pool->allocate(RSBuffer.Layout.get());
	LevelMesh.Set = LevelMesh.Pool->allocate(LevelMesh.Layout.get());
	LightTiles.Set = LightTiles.Pool->allocate(LightTiles.Layout.get());

	for (auto& set : ZMinMax.Set)
		set = ZMinMax.Pool->allocate(ZMinMax.Layout.get());

	auto rsbuffers = fb->GetBufferManager()->GetRSBuffers();
	WriteDescriptors()
		.AddBuffer(RSBuffer.Set.get(), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->Viewpoint.UBO.get(), 0, sizeof(HWViewpointUniforms))
		.AddBuffer(RSBuffer.Set.get(), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->MatrixBuffer->UBO(), 0, sizeof(MatricesUBO))
		.AddBuffer(RSBuffer.Set.get(), 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->SurfaceUniformsBuffer->UBO(), 0, sizeof(SurfaceUniformsUBO))
		.AddBuffer(RSBuffer.Set.get(), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, rsbuffers->Lightbuffer.SSO.get(), 0, sizeof(LightBufferSSO))
		.AddBuffer(RSBuffer.Set.get(), 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->Fogballbuffer.UBO.get(), 0, sizeof(FogballBufferUBO))
		.AddBuffer(RSBuffer.Set.get(), 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, rsbuffers->Bonebuffer.SSO.get())
		.Execute(fb->GetDevice());
}

void VkDescriptorSetManager::Deinit()
{
	while (!Materials.empty())
		RemoveMaterial(Materials.back());
}

void VkDescriptorSetManager::BeginFrame()
{
	UpdateFixedSet();
	UpdateLevelMeshSet();
	UpdateLightTilesSet();
	UpdateZMinMaxSet();
}

void VkDescriptorSetManager::UpdateLevelMeshSet()
{
	auto rsbuffers = fb->GetBufferManager()->GetRSBuffers();
	WriteDescriptors()
		.AddBuffer(LevelMesh.Set.get(), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->Viewpoint.UBO.get(), 0, sizeof(HWViewpointUniforms))
		.AddBuffer(LevelMesh.Set.get(), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->MatrixBuffer->UBO(), 0, sizeof(MatricesUBO))
		.AddBuffer(LevelMesh.Set.get(), 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetUniformsBuffer())
		.AddBuffer(LevelMesh.Set.get(), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetLightUniformsBuffer())
		.AddBuffer(LevelMesh.Set.get(), 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetBuffers()->SceneLightTiles.get())
		.AddBuffer(LevelMesh.Set.get(), 5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->Fogballbuffer.UBO.get(), 0, sizeof(FogballBufferUBO))
		.AddBuffer(LevelMesh.Set.get(), 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, rsbuffers->Bonebuffer.SSO.get())
		.Execute(fb->GetDevice());
}

void VkDescriptorSetManager::UpdateLightTilesSet()
{
	WriteDescriptors()
		.AddStorageImage(LightTiles.Set.get(), 0, fb->GetBuffers()->SceneZMinMax[5].View.get(), VK_IMAGE_LAYOUT_GENERAL)
		.AddBuffer(LightTiles.Set.get(), 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetDynLightBuffer())
		.AddBuffer(LightTiles.Set.get(), 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetBuffers()->SceneLightTiles.get())
		.Execute(fb->GetDevice());
}

void VkDescriptorSetManager::UpdateZMinMaxSet()
{
	WriteDescriptors()
		.AddCombinedImageSampler(ZMinMax.Set[0].get(), 0, fb->GetBuffers()->SceneDepthStencil.DepthOnlyView.get(), fb->GetSamplerManager()->ZMinMaxSampler.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		.Execute(fb->GetDevice());

	for (int i = 1; i < 6; i++)
	{
		WriteDescriptors()
			.AddCombinedImageSampler(ZMinMax.Set[i].get(), 0, fb->GetBuffers()->SceneZMinMax[i - 1].View.get(), fb->GetSamplerManager()->ZMinMaxSampler.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.Execute(fb->GetDevice());
	}
}

void VkDescriptorSetManager::UpdateFixedSet()
{
	fb->GetCommands()->DrawDeleteList->Add(std::move(Fixed.Set));

	Fixed.Set = Fixed.Pool->tryAllocate(Fixed.Layout.get());
	if (!Fixed.Set)
	{
		fb->GetCommands()->WaitForCommands(false);
		Fixed.Set = Fixed.Pool->allocate(Fixed.Layout.get());
	}

	WriteDescriptors update;
	update.AddCombinedImageSampler(Fixed.Set.get(), 0, fb->GetTextureManager()->Shadowmap.View.get(), fb->GetSamplerManager()->ShadowmapSampler.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	update.AddCombinedImageSampler(Fixed.Set.get(), 1, fb->GetBuffers()->SceneLinearDepth.View.get(), fb->GetSamplerManager()->Get(PPFilterMode::Nearest, PPWrapMode::Clamp), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	if (fb->IsRayQueryEnabled())
	{
		update.AddAccelerationStructure(Fixed.Set.get(), 2, fb->GetLevelMesh()->GetAccelStruct());
	}
	else
	{
		update.AddBuffer(Fixed.Set.get(), 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetNodeBuffer());
	}

	update.AddBuffer(Fixed.Set.get(), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetVertexBuffer());
	update.AddBuffer(Fixed.Set.get(), 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetIndexBuffer());
	update.AddBuffer(Fixed.Set.get(), 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetSurfaceIndexBuffer());
	update.AddBuffer(Fixed.Set.get(), 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetSurfaceBuffer());
	update.AddBuffer(Fixed.Set.get(), 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetPortalBuffer());

	update.Execute(fb->GetDevice());
}

void VkDescriptorSetManager::ResetHWTextureSets()
{
	for (auto mat : Materials)
		mat->DeleteDescriptors();

	for (FSWColormap* colormap : Colormaps)
	{
		if (colormap->Renderdev.bindIndex != -1)
		{
			FreeBindlessSlot(colormap->Renderdev.bindIndex);
			colormap->Renderdev.bindIndex = -1;
		}
	}
	Colormaps.clear();

	for (int index : LightProbes)
		FreeBindlessSlot(index);
	LightProbes.clear();

	// Fixed indexes the shaders can always access
	SetBindlessTexture(0, fb->GetTextureManager()->GetNullTextureView(), fb->GetSamplerManager()->Get(CLAMP_XY_NOMIP));
	SetBindlessTexture(1, fb->GetTextureManager()->GetBrdfLutTextureView(), fb->GetSamplerManager()->Get(CLAMP_XY_NOMIP));
	SetBindlessTexture(2, fb->GetTextureManager()->GetGamePaletteView(), fb->GetSamplerManager()->Get(CLAMP_XY_NOMIP));
}

void VkDescriptorSetManager::AddMaterial(VkMaterial* texture)
{
	texture->it = Materials.insert(Materials.end(), texture);
}

void VkDescriptorSetManager::RemoveMaterial(VkMaterial* texture)
{
	texture->DeleteDescriptors();
	texture->fb = nullptr;
	Materials.erase(texture->it);
}

VulkanDescriptorSet* VkDescriptorSetManager::GetInput(VkPPRenderPassSetup* passSetup, const TArray<PPTextureInput>& textures, bool bindShadowMapBuffers)
{
	auto descriptors = AllocatePPSet(passSetup->DescriptorLayout.get());
	descriptors->SetDebugName("VkPostprocess.descriptors");

	WriteDescriptors write;
	VkImageTransition imageTransition;

	for (unsigned int index = 0; index < textures.Size(); index++)
	{
		const PPTextureInput& input = textures[index];
		VulkanSampler* sampler = fb->GetSamplerManager()->Get(input.Filter, input.Wrap);
		VkTextureImage* tex = fb->GetTextureManager()->GetTexture(input.Type, input.Texture);

		write.AddCombinedImageSampler(descriptors.get(), index, tex->DepthOnlyView ? tex->DepthOnlyView.get() : tex->View.get(), sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		imageTransition.AddImage(tex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);
	}

	if (bindShadowMapBuffers)
	{
		write.AddBuffer(descriptors.get(), 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetBufferManager()->Shadowmap.Nodes->mBuffer.get());
		write.AddBuffer(descriptors.get(), 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetBufferManager()->Shadowmap.Lines->mBuffer.get());
		write.AddBuffer(descriptors.get(), 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetBufferManager()->Shadowmap.Lights->mBuffer.get());
	}

	write.Execute(fb->GetDevice());
	imageTransition.Execute(fb->GetCommands()->GetDrawCommands());

	VulkanDescriptorSet* set = descriptors.get();
	fb->GetCommands()->DrawDeleteList->Add(std::move(descriptors));
	return set;
}

std::unique_ptr<VulkanDescriptorSet> VkDescriptorSetManager::AllocatePPSet(VulkanDescriptorSetLayout* layout)
{
	if (Postprocess.Pool)
	{
		auto descriptors = Postprocess.Pool->tryAllocate(layout);
		if (descriptors)
			return descriptors;

		fb->GetCommands()->DrawDeleteList->Add(std::move(Postprocess.Pool));
	}

	Postprocess.Pool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 200)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4)
		.MaxSets(100)
		.DebugName("PPDescriptorPool")
		.Create(fb->GetDevice());

	return Postprocess.Pool->allocate(layout);
}

void VkDescriptorSetManager::CreateLevelMeshLayout()
{
	LevelMesh.Layout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
		.DebugName("VkDescriptorSetManager.LevelMesh.Layout")
		.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateRSBufferLayout()
{
	RSBuffer.Layout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
		.DebugName("VkDescriptorSetManager.RSBuffer.Layout")
		.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateFixedLayout()
{
	DescriptorSetLayoutBuilder builder;
	builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	if (fb->IsRayQueryEnabled())
	{
		builder.AddBinding(2, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	else
	{
		builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	builder.AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.AddBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.DebugName("VkDescriptorSetManager.Fixed.SetLayout");
	Fixed.Layout = builder.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateLightTilesLayout()
{
	LightTiles.Layout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.DebugName("VkDescriptorSetManager.LightTiles.Layout")
		.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateZMinMaxLayout()
{
	ZMinMax.Layout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.DebugName("VkDescriptorSetManager.ZMinMax.Layout")
		.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateLightTilesPool()
{
	LightTiles.Pool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2)
		.MaxSets(1)
		.DebugName("VkDescriptorSetManager.LightTiles.Pool")
		.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateZMinMaxPool()
{
	ZMinMax.Pool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6)
		.MaxSets(6)
		.DebugName("VkDescriptorSetManager.ZMinMax.Pool")
		.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateLevelMeshPool()
{
	LevelMesh.Pool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 3)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4)
		.MaxSets(1)
		.DebugName("VkDescriptorSetManager.LevelMesh.Pool")
		.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateRSBufferPool()
{
	RSBuffer.Pool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1)
		.MaxSets(1)
		.DebugName("VkDescriptorSetManager.RSBuffer.Pool")
		.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateFixedPool()
{
	DescriptorPoolBuilder poolbuilder;
	poolbuilder.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * MaxFixedSets);
	if (fb->IsRayQueryEnabled())
	{
		poolbuilder.AddPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 * MaxFixedSets);
		poolbuilder.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 * MaxFixedSets);
	}
	else
	{
		poolbuilder.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6 * MaxFixedSets);
	}
	poolbuilder.MaxSets(MaxFixedSets);
	poolbuilder.DebugName("VkDescriptorSetManager.Fixed.Pool");
	Fixed.Pool = poolbuilder.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateBindlessSet()
{
	Bindless.Pool = DescriptorPoolBuilder()
		.Flags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxBindlessTextures)
		.MaxSets(MaxBindlessTextures)
		.DebugName("Bindless.Pool")
		.Create(fb->GetDevice());

	Bindless.Layout = DescriptorSetLayoutBuilder()
		.Flags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT)
		.AddBinding(
			0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			MaxBindlessTextures,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT)
		.DebugName("Bindless.Layout")
		.Create(fb->GetDevice());

	Bindless.Set = Bindless.Pool->allocate(Bindless.Layout.get(), MaxBindlessTextures);
}

void VkDescriptorSetManager::UpdateBindlessDescriptorSet()
{
	auto sampler = fb->GetSamplerManager()->LightmapSampler.get();
	int index = FixedBindlessSlots;
	for (auto& lightmap : fb->GetTextureManager()->Lightmaps)
	{
		Bindless.Writer.AddCombinedImageSampler(Bindless.Set.get(), 0, index, lightmap.Light.View.get(), sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		Bindless.Writer.AddCombinedImageSampler(Bindless.Set.get(), 0, index + 1, lightmap.Probe.View.get(), sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		index += 2;
	}

	Bindless.Writer.Execute(fb->GetDevice());
	Bindless.Writer = WriteDescriptors();
}

int VkDescriptorSetManager::AllocBindlessSlot(int count)
{
	if (count <= 0)
		return 0;

	int bucket = count - 1;

	if (Bindless.FreeSlots.size() <= (size_t)bucket)
		Bindless.FreeSlots.resize(bucket + 1);

	if (!Bindless.FreeSlots[bucket].empty())
	{
		int index = Bindless.FreeSlots[bucket].back();
		Bindless.FreeSlots[bucket].pop_back();
		return index;
	}
	else
	{
		if (Bindless.NextIndex + count > MaxBindlessTextures)
			I_FatalError("Out of bindless texture slots!");
		int index = Bindless.NextIndex;
		if (Bindless.AllocSizes.size() < index + count)
			Bindless.AllocSizes.resize(index + count, 0);
		Bindless.AllocSizes[index] = count;
		Bindless.NextIndex += count;
		return index;
	}
}

void VkDescriptorSetManager::FreeBindlessSlot(int index)
{
	if (index <= 0)
		return;

	int bucket = Bindless.AllocSizes[index] - 1;
	Bindless.FreeSlots[bucket].push_back(index);
}

void VkDescriptorSetManager::SetBindlessTexture(int index, VulkanImageView* imageview, VulkanSampler* sampler)
{
	Bindless.Writer.AddCombinedImageSampler(Bindless.Set.get(), 0, index, imageview, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

int VkDescriptorSetManager::GetSWColormapTextureIndex(FSWColormap* colormap)
{
	if (colormap->Renderdev.bindIndex != -1)
		return colormap->Renderdev.bindIndex;

	colormap->Renderdev.bindIndex = AllocBindlessSlot(1);
	SetBindlessTexture(colormap->Renderdev.bindIndex, fb->GetTextureManager()->GetSWColormapView(colormap), fb->GetSamplerManager()->Get(CLAMP_XY_NOMIP));
	Colormaps.push_back(colormap);
	return colormap->Renderdev.bindIndex;
}

int VkDescriptorSetManager::GetLightProbeTextureIndex(int probeIndex)
{
	if ((size_t)probeIndex >= LightProbes.size())
		LightProbes.resize(probeIndex + 1, -1);

	if (LightProbes[probeIndex] == -1)
	{
		auto textures = fb->GetTextureManager();

		// Seems we might be rendering a probe before we have data for it
		if (textures->Irradiancemaps.size() > (size_t)probeIndex && textures->Irradiancemaps[probeIndex].View &&
			textures->Prefiltermaps.size() > (size_t)probeIndex && textures->Prefiltermaps[probeIndex].View)
		{
			int bindIndex = AllocBindlessSlot(2);
			LightProbes[probeIndex] = bindIndex;
			SetBindlessTexture(bindIndex, textures->Irradiancemaps[probeIndex].View.get(), fb->GetSamplerManager()->IrradiancemapSampler.get());
			SetBindlessTexture(bindIndex + 1, textures->Prefiltermaps[probeIndex].View.get(), fb->GetSamplerManager()->PrefiltermapSampler.get());
		}
		else
		{
			return 0;
		}
	}

	return LightProbes[probeIndex];
}
