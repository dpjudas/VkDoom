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

VkDescriptorSetManager::VkDescriptorSetManager(VulkanRenderDevice* fb) : fb(fb)
{
	CreateLevelMeshLayout();
	CreateRSBufferLayout();
	CreateFixedLayout();
	CreateLevelMeshPool();
	CreateRSBufferPool();
	CreateFixedPool();
	CreateBindlessSet();
}

VkDescriptorSetManager::~VkDescriptorSetManager()
{
	while (!Materials.empty())
		RemoveMaterial(Materials.back());
}

void VkDescriptorSetManager::Init()
{
	UpdateFixedSet();

	RSBuffer.Set = RSBuffer.Pool->allocate(RSBuffer.Layout.get());
	LevelMesh.Set = LevelMesh.Pool->allocate(LevelMesh.Layout.get());

	auto rsbuffers = fb->GetBufferManager()->GetRSBuffers();
	WriteDescriptors()
		.AddBuffer(RSBuffer.Set.get(), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->Viewpoint.UBO.get(), 0, sizeof(HWViewpointUniforms))
		.AddBuffer(RSBuffer.Set.get(), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->MatrixBuffer->UBO(), 0, sizeof(MatricesUBO))
		.AddBuffer(RSBuffer.Set.get(), 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->SurfaceUniformsBuffer->UBO(), 0, sizeof(SurfaceUniformsUBO))
		.AddBuffer(RSBuffer.Set.get(), 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->Lightbuffer.UBO.get(), 0, sizeof(LightBufferUBO))
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
}

void VkDescriptorSetManager::UpdateLevelMeshSet()
{
	auto rsbuffers = fb->GetBufferManager()->GetRSBuffers();
	WriteDescriptors()
		.AddBuffer(LevelMesh.Set.get(), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->Viewpoint.UBO.get(), 0, sizeof(HWViewpointUniforms))
		.AddBuffer(LevelMesh.Set.get(), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->MatrixBuffer->UBO(), 0, sizeof(MatricesUBO))
		.AddBuffer(LevelMesh.Set.get(), 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetUniformsBuffer())
		.AddBuffer(LevelMesh.Set.get(), 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->Lightbuffer.UBO.get(), 0, sizeof(LightBufferUBO))
		.AddBuffer(LevelMesh.Set.get(), 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsbuffers->Fogballbuffer.UBO.get(), 0, sizeof(FogballBufferUBO))
		.AddBuffer(LevelMesh.Set.get(), 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, rsbuffers->Bonebuffer.SSO.get())
		.Execute(fb->GetDevice());
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
	update.AddCombinedImageSampler(Fixed.Set.get(), 1, fb->GetTextureManager()->Lightmap.View.get(), fb->GetSamplerManager()->LightmapSampler.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	if (fb->GetDevice()->SupportsExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME) && fb->GetDevice()->PhysicalDevice.Features.RayQuery.rayQuery)
	{
		update.AddAccelerationStructure(Fixed.Set.get(), 2, fb->GetLevelMesh()->GetAccelStruct());
	}
	else
	{
		update.AddBuffer(Fixed.Set.get(), 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetNodeBuffer());
		update.AddBuffer(Fixed.Set.get(), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetVertexBuffer());
		update.AddBuffer(Fixed.Set.get(), 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fb->GetLevelMesh()->GetIndexBuffer());
	}
	update.Execute(fb->GetDevice());
}

void VkDescriptorSetManager::ResetHWTextureSets()
{
	for (auto mat : Materials)
		mat->DeleteDescriptors();

	auto deleteList = fb->GetCommands()->DrawDeleteList.get();
	for (auto& desc : Texture.Pools)
	{
		deleteList->Add(std::move(desc));
	}
	deleteList->Add(std::move(Texture.NullSet));

	Texture.Pools.clear();
	Texture.SetsLeft = 0;
	Texture.DescriptorsLeft = 0;

	Bindless.Writer = WriteDescriptors();
	Bindless.NextIndex = 0;

	// Slot zero always needs to be the null texture
	AddBindlessTextureIndex(fb->GetTextureManager()->GetNullTextureView(), fb->GetSamplerManager()->Get(CLAMP_XY_NOMIP));
}

VulkanDescriptorSet* VkDescriptorSetManager::GetNullTextureSet()
{
	if (!Texture.NullSet)
	{
		Texture.NullSet = AllocateTextureSet(SHADER_MIN_REQUIRED_TEXTURE_LAYERS);

		WriteDescriptors update;
		for (int i = 0; i < SHADER_MIN_REQUIRED_TEXTURE_LAYERS; i++)
		{
			update.AddCombinedImageSampler(Texture.NullSet.get(), i, fb->GetTextureManager()->GetNullTextureView(), fb->GetSamplerManager()->Get(CLAMP_XY_NOMIP), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		update.Execute(fb->GetDevice());
	}

	return Texture.NullSet.get();
}

std::unique_ptr<VulkanDescriptorSet> VkDescriptorSetManager::AllocateTextureSet(int numLayers)
{
	if (Texture.SetsLeft == 0 || Texture.DescriptorsLeft < numLayers)
	{
		Texture.SetsLeft = 1000;
		Texture.DescriptorsLeft = 2000;

		Texture.Pools.push_back(DescriptorPoolBuilder()
			.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, Texture.DescriptorsLeft)
			.MaxSets(Texture.SetsLeft)
			.DebugName("VkDescriptorSetManager.Texture.Pool")
			.Create(fb->GetDevice()));
	}

	Texture.SetsLeft--;
	Texture.DescriptorsLeft -= numLayers;
	return Texture.Pools.back()->allocate(GetTextureLayout(numLayers));
}

VulkanDescriptorSetLayout* VkDescriptorSetManager::GetTextureLayout(int numLayers)
{
	if (Texture.Layouts.size() < (size_t)numLayers)
		Texture.Layouts.resize(numLayers);

	auto& layout = Texture.Layouts[numLayers - 1];
	if (layout)
		return layout.get();

	DescriptorSetLayoutBuilder builder;
	for (int i = 0; i < numLayers; i++)
	{
		builder.AddBinding(i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	builder.DebugName("VkDescriptorSetManager.Texture.SetLayout");
	layout = builder.Create(fb->GetDevice());
	return layout.get();
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
		.AddBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
		.DebugName("VkDescriptorSetManager.LevelMesh.Layout")
		.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateRSBufferLayout()
{
	RSBuffer.Layout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
		.DebugName("VkDescriptorSetManager.RSBuffer.Layout")
		.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateFixedLayout()
{
	DescriptorSetLayoutBuilder builder;
	builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	if (fb->GetDevice()->SupportsExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME) && fb->GetDevice()->PhysicalDevice.Features.RayQuery.rayQuery)
	{
		builder.AddBinding(2, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	else
	{
		builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		builder.AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		builder.AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	builder.DebugName("VkDescriptorSetManager.Fixed.SetLayout");
	Fixed.Layout = builder.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateLevelMeshPool()
{
	LevelMesh.Pool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2)
		.MaxSets(1)
		.DebugName("VkDescriptorSetManager.LevelMesh.Pool")
		.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateRSBufferPool()
{
	RSBuffer.Pool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 5)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1)
		.MaxSets(1)
		.DebugName("VkDescriptorSetManager.RSBuffer.Pool")
		.Create(fb->GetDevice());
}

void VkDescriptorSetManager::CreateFixedPool()
{
	DescriptorPoolBuilder poolbuilder;
	poolbuilder.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * MaxFixedSets);
	if (fb->GetDevice()->SupportsExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME) && fb->GetDevice()->PhysicalDevice.Features.RayQuery.rayQuery)
	{
		poolbuilder.AddPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 * MaxFixedSets);
	}
	else
	{
		poolbuilder.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * MaxFixedSets);
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
			VK_SHADER_STAGE_FRAGMENT_BIT,
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT)
		.DebugName("Bindless.Layout")
		.Create(fb->GetDevice());

	Bindless.Set = Bindless.Pool->allocate(Bindless.Layout.get(), MaxBindlessTextures);
}

void VkDescriptorSetManager::UpdateBindlessDescriptorSet()
{
	Bindless.Writer.Execute(fb->GetDevice());
	Bindless.Writer = WriteDescriptors();
}

int VkDescriptorSetManager::AddBindlessTextureIndex(VulkanImageView* imageview, VulkanSampler* sampler)
{
	int index = Bindless.NextIndex++;
	Bindless.Writer.AddCombinedImageSampler(Bindless.Set.get(), 0, index, imageview, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	return index;
}
