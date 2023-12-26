
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
	float Padding;
};

struct PortalInfo
{
	VSMatrix transformation;
};

struct SubmeshBufferRange
{
	int Offset = 0;
	int Size = 0;
};

struct SubmeshBufferLocation
{
	LevelSubmesh* Submesh = nullptr;
	SubmeshBufferRange Vertex;
	SubmeshBufferRange Index;
	SubmeshBufferRange Node;
	SubmeshBufferRange SurfaceIndex;
	SubmeshBufferRange Surface;
	SubmeshBufferRange UniformIndexes;
	SubmeshBufferRange Uniforms;
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
	void CreateTopLevelAS();

	void UploadMeshes(bool dynamicOnly);
	void UploadTLASInstanceBuffer();
	void UpdateTopLevelAS();

	BLAS CreateBLAS(LevelSubmesh *submesh, bool preferFastBuild, int vertexOffset, int indexOffset);

	int GetMaxVertexBufferSize();
	int GetMaxIndexBufferSize();
	int GetMaxNodeBufferSize();
	int GetMaxSurfaceBufferSize();
	int GetMaxUniformsBufferSize();
	int GetMaxSurfaceIndexBufferSize();

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

	std::unique_ptr<VulkanBuffer> NodeBuffer;

	TArray<FFlatVertex> Vertices;
	static const int MaxDynamicVertices = 100'000;
	static const int MaxDynamicIndexes = 100'000;
	static const int MaxDynamicSurfaces = 100'000;
	static const int MaxDynamicUniforms = 100'000;
	static const int MaxDynamicSurfaceIndexes = 25'000;
	static const int MaxDynamicNodes = 10'000;

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

	void Upload(bool dynamicOnly);

private:
	void BeginTransfer(size_t transferBufferSize);
	void EndTransfer(size_t transferBufferSize);
	void UploadNodes();
	void UploadVertices();
	void UploadUniformIndexes();
	void UploadIndexes();
	void UploadSurfaceIndexes();
	void UploadSurfaces();
	void UploadUniforms();
	void UploadPortals();
	void UpdateSizes();
	void UpdateLocations();
	size_t GetTransferSize();

	VkLevelMesh* Mesh;
	TArray<SubmeshBufferLocation> locations;
	unsigned int start = 0;
	unsigned int end = 0;
	uint8_t* data = nullptr;
	size_t datapos = 0;
	VulkanCommandBuffer* cmdbuffer = nullptr;
	std::unique_ptr<VulkanBuffer> transferBuffer;
};
