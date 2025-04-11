
#pragma once

#include <zvulkan/vulkanobjects.h>
#include "vulkan/textures/vk_imagetransition.h"
#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

class VulkanRenderDevice;
class VkHardwareTexture;
class VkMaterial;
class VkPPTexture;
class VkTextureImage;
enum class PPTextureType;
class PPTexture;

class VkTextureManager
{
public:
	VkTextureManager(VulkanRenderDevice* fb);
	~VkTextureManager();

	void Deinit();

	void BeginFrame();

	void CreateLightmap(int size, int count, TArray<uint16_t>&& data);
	void CreateIrradiancemap(int size, int count, const TArray<uint16_t>& data);
	void CreatePrefiltermap(int size, int count, const TArray<uint16_t>& data);
	void DownloadLightmap(int arrayIndex, uint16_t* buffer);

	VkTextureImage* GetTexture(const PPTextureType& type, PPTexture* tex);
	VkFormat GetTextureFormat(PPTexture* texture);

	void AddTexture(VkHardwareTexture* texture);
	void RemoveTexture(VkHardwareTexture* texture);

	void AddPPTexture(VkPPTexture* texture);
	void RemovePPTexture(VkPPTexture* texture);

	VulkanImage* GetNullTexture() { return NullTexture.get(); }
	VulkanImageView* GetNullTextureView() { return NullTextureView.get(); }

	VulkanImage* GetBrdfLutTexture() { return BrdfLutTexture.get(); }
	VulkanImageView* GetBrdfLutTextureView() { return BrdfLutTextureView.get(); }

	int GetHWTextureCount() { return (int)Textures.size(); }

	VkTextureImage Shadowmap;

	struct
	{
		VkTextureImage Image;
		int Size = 0;
		int Count = 0;
	} Lightmap, Irradiancemap, Prefiltermap;

	static const int MAX_REFLECTION_LOD = 4; // Note: must match what lightmodel_pbr.glsl expects

	void ProcessMainThreadTasks();
	void RunOnWorkerThread(std::function<void()> task);
	void RunOnMainThread(std::function<void()> task);

	int CreateUploadID(VkHardwareTexture* tex);
	bool CheckUploadID(int id);

private:
	void CreateNullTexture();
	void CreateBrdfLutTexture();
	void CreateShadowmap();
	void CreateLightmap();
	void CreateIrradiancemap();
	void CreatePrefiltermap();

	void StartWorkerThread();
	void StopWorkerThread();
	void WorkerThreadMain();

	VkPPTexture* GetVkTexture(PPTexture* texture);

	VulkanRenderDevice* fb = nullptr;

	std::list<VkHardwareTexture*> Textures;
	std::list<VkPPTexture*> PPTextures;

	std::unique_ptr<VulkanImage> NullTexture;
	std::unique_ptr<VulkanImageView> NullTextureView;

	std::unique_ptr<VulkanImage> BrdfLutTexture;
	std::unique_ptr<VulkanImageView> BrdfLutTextureView;

	int NextUploadID = 1;
	std::unordered_map<int, VkHardwareTexture*> PendingUploads;

	struct
	{
		std::thread Thread;
		std::mutex Mutex;
		std::condition_variable CondVar;
		bool StopFlag = false;
		std::list<std::function<void()>> WorkerTasks;
		std::vector<std::function<void()>> MainTasks;
	} Worker;
};
