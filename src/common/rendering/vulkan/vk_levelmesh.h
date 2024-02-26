
#pragma once

#include "zvulkan/vulkanobjects.h"
#include "hw_levelmesh.h"
#include "common/utility/matrix.h"

class VulkanRenderDevice;

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
	float Padding2;
	FVector3 Color;
	float Padding3;
};

static_assert(sizeof(LightInfo) == sizeof(float) * 20);

struct MeshBufferRange
{
	int Offset = 0;
	int Size = 0;
};

class VkLevelMesh
{
public:
	VkLevelMesh(VulkanRenderDevice* fb);

	void SetLevelMesh(LevelMesh* mesh);
	void BeginFrame();

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

	LevelMesh* GetMesh() { return Mesh; }

private:
	struct BLAS
	{
		std::unique_ptr<VulkanBuffer> ScratchBuffer;
		std::unique_ptr<VulkanBuffer> AccelStructBuffer;
		std::unique_ptr<VulkanAccelerationStructure> AccelStruct;
	};

	void Reset();
	void CreateVulkanObjects();
	void CreateBuffers();
	void CreateStaticBLAS();
	void CreateDynamicBLAS();
	void CreateTLASInstanceBuffer();
	void CreateTopLevelAS(int instanceCount);

	void UploadMeshes(bool dynamicOnly);
	void UploadTLASInstanceBuffer();
	void UpdateTopLevelAS(int instanceCount);

	BLAS CreateBLAS(bool preferFastBuild, int indexOffset, int indexCount);

	VulkanRenderDevice* fb = nullptr;

	bool useRayQuery = true;

	LevelMesh NullMesh;
	LevelMesh* Mesh = nullptr;

	struct
	{
		TArray<MeshBufferRange> Vertex;
		TArray<MeshBufferRange> Index;
		TArray<MeshBufferRange> Node;
		TArray<MeshBufferRange> SurfaceIndex;
		TArray<MeshBufferRange> Surface;
		TArray<MeshBufferRange> UniformIndexes;
		TArray<MeshBufferRange> Uniforms;
		TArray<MeshBufferRange> Portals;
		TArray<MeshBufferRange> Light;
		TArray<MeshBufferRange> LightIndex;
	} Locations;

	std::unique_ptr<VulkanBuffer> VertexBuffer;
	std::unique_ptr<VulkanBuffer> UniformIndexBuffer;
	std::unique_ptr<VulkanBuffer> IndexBuffer;
	std::unique_ptr<VulkanBuffer> SurfaceIndexBuffer;
	std::unique_ptr<VulkanBuffer> SurfaceBuffer;
	std::unique_ptr<VulkanBuffer> UniformsBuffer;
	std::unique_ptr<VulkanBuffer> PortalBuffer;
	std::unique_ptr<VulkanBuffer> LightBuffer;
	std::unique_ptr<VulkanBuffer> LightIndexBuffer;

	std::unique_ptr<VulkanBuffer> NodeBuffer;

	BLAS StaticBLAS;
	BLAS DynamicBLAS;

	struct
	{
		std::unique_ptr<VulkanBuffer> TransferBuffer;
		std::unique_ptr<VulkanBuffer> ScratchBuffer;
		std::unique_ptr<VulkanBuffer> InstanceBuffer;
		std::unique_ptr<VulkanBuffer> AccelStructBuffer;
		std::unique_ptr<VulkanAccelerationStructure> AccelStruct;
	} TopLevelAS;

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
	VulkanCommandBuffer* cmdbuffer = nullptr;
	std::unique_ptr<VulkanBuffer> transferBuffer;
};
