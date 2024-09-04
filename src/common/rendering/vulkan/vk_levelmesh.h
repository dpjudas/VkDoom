
#pragma once

#include "zvulkan/vulkanobjects.h"
#include "hw_levelmesh.h"
#include "common/utility/matrix.h"
#include <map>

class VulkanRenderDevice;
class ShaderIncludeResult;
class VkRenderPassKey;

struct CollisionNodeBufferHeader
{
	int root;
	int padding1;
	int padding2;
	int padding3;
};

struct CollisionNode
{
	FVector3 center;
	float padding1;
	FVector3 extents;
	float padding2;
	int left;
	int right;
	int element_index;
	int padding3;
};

struct SurfaceInfo
{
	FVector3 Normal;
	float Sky;
	uint32_t PortalIndex;
	int32_t TextureIndex;
	float Alpha;
	float Padding0;
	uint32_t LightStart;
	uint32_t LightEnd;
	uint32_t Padding1;
	uint32_t Padding2;
};

struct PortalInfo
{
	VSMatrix transformation;
};

struct LightInfo
{
	FVector3 Origin;
	float Padding0;
	FVector3 RelativeOrigin;
	float Padding1;
	float Radius;
	float Intensity;
	float InnerAngleCos;
	float OuterAngleCos;
	FVector3 SpotDir;
	float SoftShadowRadius;
	FVector3 Color;
	float Padding3;
};

struct ViewerPushConstants
{
	VSMatrix ViewToWorld;
	FVector3 CameraPos;
	float ProjX;
	FVector3 SunDir;
	float ProjY;
	FVector3 SunColor;
	float SunIntensity;
};

static_assert(sizeof(LightInfo) == sizeof(float) * 20);

class VkLevelMesh
{
public:
	VkLevelMesh(VulkanRenderDevice* fb);

	void SetLevelMesh(LevelMesh* mesh);
	void BeginFrame();

	void RaytraceScene(const VkRenderPassKey& renderPassKey, VulkanCommandBuffer* commands, const FVector3& cameraPos, const VSMatrix& viewToWorld, float fovy, float aspect);

	VulkanAccelerationStructure* GetAccelStruct() { return TopLevelAS.AccelStruct.get(); }
	VulkanBuffer* GetVertexBuffer() { return VertexBuffer.get(); }
	VulkanBuffer* GetUniformIndexBuffer() { return UniformIndexBuffer.get(); }
	VulkanBuffer* GetIndexBuffer() { return IndexBuffer.get(); }
	VulkanBuffer* GetNodeBuffer() { return NodeBuffer.get(); }
	VulkanBuffer* GetSurfaceIndexBuffer() { return SurfaceIndexBuffer.get(); }
	VulkanBuffer* GetSurfaceBuffer() { return SurfaceBuffer.get(); }
	VulkanBuffer* GetUniformsBuffer() { return UniformsBuffer.get(); }
	VulkanBuffer* GetPortalBuffer() { return PortalBuffer.get(); }
	VulkanBuffer* GetLightBuffer() { return LightBuffer.get(); }
	VulkanBuffer* GetLightIndexBuffer() { return LightIndexBuffer.get(); }
	VulkanBuffer* GetDrawIndexBuffer() { return DrawIndexBuffer.get(); }

	LevelMesh* GetMesh() { return Mesh; }

private:
	struct BLAS
	{
		std::unique_ptr<VulkanBuffer> ScratchBuffer;
		std::unique_ptr<VulkanBuffer> AccelStructBuffer;
		std::unique_ptr<VulkanAccelerationStructure> AccelStruct;
		VkDeviceAddress DeviceAddress = 0;
		int InstanceCustomIndex = 0;
		bool NeedsUpdate = false;
	};

	void Reset();
	void CreateVulkanObjects();
	void CreateBuffers();
	void CreateTLASInstanceBuffer();
	void CreateTopLevelAS(int instanceCount);

	void UploadMeshes();
	void UploadTLASInstanceBuffer();
	void UpdateTopLevelAS(int instanceCount);

	BLAS CreateBLAS(bool preferFastBuild, int indexOffset, int indexCount);

	void CreateViewerObjects();

	static FString LoadPrivateShaderLump(const char* lumpname);
	static FString LoadPublicShaderLump(const char* lumpname);
	static ShaderIncludeResult OnInclude(FString headerName, FString includerName, size_t depth, bool system);

	VulkanRenderDevice* fb = nullptr;

	bool useRayQuery = true;

	LevelMesh NullMesh;
	LevelMesh* Mesh = nullptr;

	std::unique_ptr<VulkanBuffer> VertexBuffer;
	std::unique_ptr<VulkanBuffer> UniformIndexBuffer;
	std::unique_ptr<VulkanBuffer> IndexBuffer;
	std::unique_ptr<VulkanBuffer> SurfaceIndexBuffer;
	std::unique_ptr<VulkanBuffer> SurfaceBuffer;
	std::unique_ptr<VulkanBuffer> UniformsBuffer;
	std::unique_ptr<VulkanBuffer> PortalBuffer;
	std::unique_ptr<VulkanBuffer> LightBuffer;
	std::unique_ptr<VulkanBuffer> LightIndexBuffer;
	std::unique_ptr<VulkanBuffer> DrawIndexBuffer;

	std::unique_ptr<VulkanBuffer> NodeBuffer;

	std::vector<BLAS> DynamicBLAS;
	int IndexesPerBLAS = 0;
	int InstanceCount = 0;

	struct
	{
		std::unique_ptr<VulkanBuffer> TransferBuffer;
		std::unique_ptr<VulkanBuffer> ScratchBuffer;
		std::unique_ptr<VulkanBuffer> InstanceBuffer;
		std::unique_ptr<VulkanBuffer> AccelStructBuffer;
		std::unique_ptr<VulkanAccelerationStructure> AccelStruct;
	} TopLevelAS;

	struct
	{
		std::unique_ptr<VulkanDescriptorSetLayout> DescriptorSetLayout;
		std::unique_ptr<VulkanDescriptorPool> DescriptorPool;
		std::unique_ptr<VulkanDescriptorSet> DescriptorSet;
		std::unique_ptr<VulkanShader> VertexShader;
		std::unique_ptr<VulkanShader> FragmentShader;
		//std::unique_ptr<VulkanRenderPass> RenderPass;
		std::unique_ptr<VulkanPipelineLayout> PipelineLayout;
		std::map<VkRenderPassKey, std::unique_ptr<VulkanPipeline>> Pipeline;
	} Viewer;

	friend class VkLevelMeshUploader;
};

class VkLevelMeshUploader
{
public:
	VkLevelMeshUploader(VkLevelMesh* mesh);

	void Upload();

private:
	void BeginTransfer(size_t transferBufferSize);
	void EndTransfer(size_t transferBufferSize);
	size_t GetTransferSize();
	void ClearRanges();

	void UploadNodes();
	void UploadSurfaces();
	void UploadUniforms();
	void UploadPortals();
	void UploadLights();

	template<typename T>
	void UploadRanges(const TArray<MeshBufferRange>& ranges, const T* srcbuffer, VulkanBuffer* destbuffer);

	VkLevelMesh* Mesh = nullptr;
	uint8_t* data = nullptr;
	size_t datapos = 0;
	std::unique_ptr<VulkanBuffer> transferBuffer;

	struct CopyCommand
	{
		CopyCommand() = default;
		CopyCommand(VulkanBuffer* srcBuffer, VulkanBuffer* dstBuffer, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0, VkDeviceSize size = VK_WHOLE_SIZE)
			: srcBuffer(srcBuffer), dstBuffer(dstBuffer), srcOffset(srcOffset), dstOffset(dstOffset), size(size) { }

		VulkanBuffer* srcBuffer = nullptr;
		VulkanBuffer* dstBuffer = nullptr;
		VkDeviceSize srcOffset = 0;
		VkDeviceSize dstOffset = 0;
		VkDeviceSize size = VK_WHOLE_SIZE;
	};
	std::vector<CopyCommand> copyCommands;
};
