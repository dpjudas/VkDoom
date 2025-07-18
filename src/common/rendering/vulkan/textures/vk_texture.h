
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
struct FSWColormap;

class VkTextureManager
{
public:
	VkTextureManager(VulkanRenderDevice* fb);
	~VkTextureManager();

	void Deinit();

	void BeginFrame(int lightmapTextureSize, int lightmapCount);

	void SetLightmapCount(int size, int count);

	void CreateLightmap(int size, int count, const TArray<uint16_t>& data);
	void DownloadLightmap(int arrayIndex, uint16_t* buffer);

	void ResetLightProbes();
	void UploadIrradiancemap(int cubeCount, const TArray<uint16_t>& data);
	void UploadPrefiltermap(int cubeCount, const TArray<uint16_t>& data);
	void CopyIrradiancemap(const std::vector<std::unique_ptr<VulkanImage>>& probes);
	void CopyPrefiltermap(const std::vector<std::unique_ptr<VulkanImage>>& probes);
	void DownloadIrradiancemap(int probeCount, TArrayView<uint16_t> irradianceMaps);
	void DownloadPrefiltermap(int probeCount, TArrayView<uint16_t> prefilterMaps);

	void CheckIrradiancemapSize(int cubeCount);
	void CheckPrefiltermapSize(int cubeCount);

	void SetGamePalette();

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

	VulkanImage* GetGamePalette() { return GamePalette.get(); }
	VulkanImageView* GetGamePaletteView() { return GamePaletteView.get(); }

	VulkanImageView* GetSWColormapView(FSWColormap* colormap);

	int GetHWTextureCount() { return (int)Textures.size(); }

	struct Lightmap
	{
		VkTextureImage Light;
		VkTextureImage Probe;
	};

	VkTextureImage Shadowmap;
	std::vector<Lightmap> Lightmaps;
	std::vector<VkTextureImage> Irradiancemaps;
	std::vector<VkTextureImage> Prefiltermaps;

	static const int MAX_REFLECTION_LOD = 4; // Note: must match what lightmodel_pbr.glsl expects

	void ProcessMainThreadTasks();
	void RunOnWorkerThread(std::function<void()> task);
	void RunOnMainThread(std::function<void()> task);

	int CreateUploadID(VkHardwareTexture* tex);
	bool CheckUploadID(int id);

	static const int PrefiltermapSize = 128;
	static const int IrradiancemapSize = 32;

private:
	void CreateNullTexture();
	void CreateBrdfLutTexture();
	void CreateGamePalette();
	void CreateShadowmap();
	void CreateLightmap();
	void CreateIrradiancemap();
	void CreatePrefiltermap();
	void DownloadTexture(VkTextureImage* texture, uint16_t* buffer);

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

	std::unique_ptr<VulkanImage> GamePalette;
	std::unique_ptr<VulkanImageView> GamePaletteView;

	struct SWColormapTexture
	{
		std::unique_ptr<VulkanImage> Texture;
		std::unique_ptr<VulkanImageView> View;
	};
	std::vector<SWColormapTexture> Colormaps;

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
