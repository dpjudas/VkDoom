
#pragma once

#include "zvulkan/vulkanobjects.h"
#include "zvulkan/vulkanbuilders.h"
#include <list>
#include "tarray.h"

class VulkanRenderDevice;
class VkMaterial;
class PPTextureInput;
class VkPPRenderPassSetup;

class VkDescriptorSetManager
{
public:
	VkDescriptorSetManager(VulkanRenderDevice* fb);
	~VkDescriptorSetManager();

	void Init();
	void Deinit();
	void BeginFrame();
	void ResetHWTextureSets();
	void UpdateLevelMeshSet();

	VulkanDescriptorSetLayout* GetLevelMeshLayout() { return LevelMesh.Layout.get(); }
	VulkanDescriptorSetLayout* GetRSBufferLayout() { return RSBuffer.Layout.get(); }
	VulkanDescriptorSetLayout* GetFixedLayout() { return Fixed.Layout.get(); }
	VulkanDescriptorSetLayout* GetBindlessLayout() { return Bindless.Layout.get(); }

	VulkanDescriptorSet* GetLevelMeshSet() { return LevelMesh.Set.get(); }
	VulkanDescriptorSet* GetRSBufferSet() { return RSBuffer.Set.get(); }
	VulkanDescriptorSet* GetFixedSet() { return Fixed.Set.get(); }
	VulkanDescriptorSet* GetBindlessSet() { return Bindless.Set.get(); }

	VulkanDescriptorSet* GetInput(VkPPRenderPassSetup* passSetup, const TArray<PPTextureInput>& textures, bool bindShadowMapBuffers);

	void AddMaterial(VkMaterial* texture);
	void RemoveMaterial(VkMaterial* texture);

	void UpdateBindlessDescriptorSet();
	int AddBindlessTextureIndex(VulkanImageView* imageview, VulkanSampler* sampler);

private:
	void CreateLevelMeshLayout();
	void CreateRSBufferLayout();
	void CreateFixedLayout();
	void CreateLevelMeshPool();
	void CreateRSBufferPool();
	void CreateFixedPool();
	void CreateBindlessSet();
	void UpdateFixedSet();

	std::unique_ptr<VulkanDescriptorSet> AllocatePPSet(VulkanDescriptorSetLayout* layout);

	VulkanRenderDevice* fb = nullptr;

	struct
	{
		std::unique_ptr<VulkanDescriptorSetLayout> Layout;
		std::unique_ptr<VulkanDescriptorPool> Pool;
		std::unique_ptr<VulkanDescriptorSet> Set;
	} LevelMesh;

	struct
	{
		std::unique_ptr<VulkanDescriptorSetLayout> Layout;
		std::unique_ptr<VulkanDescriptorPool> Pool;
		std::unique_ptr<VulkanDescriptorSet> Set;
	} RSBuffer;

	struct
	{
		std::unique_ptr<VulkanDescriptorSetLayout> Layout;
		std::unique_ptr<VulkanDescriptorPool> Pool;
		std::unique_ptr<VulkanDescriptorSet> Set;
	} Fixed;

	struct
	{
		std::unique_ptr<VulkanDescriptorPool> Pool;
		std::unique_ptr<VulkanDescriptorSet> Set;
		std::unique_ptr<VulkanDescriptorSetLayout> Layout;
		WriteDescriptors Writer;
		int NextIndex = 0;
	} Bindless;

	struct
	{
		std::unique_ptr<VulkanDescriptorPool> Pool;
	} Postprocess;

	std::list<VkMaterial*> Materials;

	static const int MaxFixedSets = 100;
	static const int MaxBindlessTextures = 16536;
};
