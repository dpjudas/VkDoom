
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
	float SamplingDistance;
	uint32_t PortalIndex;
	int32_t TextureIndex;
	float Alpha;
};

struct SurfaceVertex
{
	FVector4 pos;
	FVector2 uv;
	float Padding1, Padding2;
};

struct PortalInfo
{
	VSMatrix transformation;
};

struct SubmeshBufferLocation
{
	LevelSubmesh* Submesh = nullptr;
	int VertexOffset = 0;
	int VertexSize = 0;
	int IndexOffset = 0;
	int IndexSize = 0;
	int NodeOffset = 0;
	int NodeSize = 0;
	int SurfaceIndexOffset = 0;
	int SurfaceIndexSize = 0;
	int SurfaceOffset = 0;
	int SurfaceSize = 0;
};

class VkRaytrace
{
public:
	VkRaytrace(VulkanRenderDevice* fb);

	void SetLevelMesh(LevelMesh* mesh);
	void BeginFrame();

	VulkanAccelerationStructure* GetAccelStruct() { return TopLevelAS.AccelStruct.get(); }
	VulkanBuffer* GetVertexBuffer() { return VertexBuffer.get(); }
	VulkanBuffer* GetIndexBuffer() { return IndexBuffer.get(); }
	VulkanBuffer* GetNodeBuffer() { return NodeBuffer.get(); }
	VulkanBuffer* GetSurfaceIndexBuffer() { return SurfaceIndexBuffer.get(); }
	VulkanBuffer* GetSurfaceBuffer() { return SurfaceBuffer.get(); }
	VulkanBuffer* GetPortalBuffer() { return PortalBuffer.get(); }

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
	void CreateTopLevelAS();
	void UploadMeshes(bool dynamicOnly);
	void UpdateDynamicBLAS();
	void UpdateTopLevelAS();

	BLAS CreateBLAS(LevelSubmesh *submesh, bool preferFastBuild, int vertexOffset, int indexOffset);

	int GetMaxVertexBufferSize();
	int GetMaxIndexBufferSize();
	int GetMaxNodeBufferSize();
	int GetMaxSurfaceBufferSize();
	int GetMaxSurfaceIndexBufferSize();

	VulkanRenderDevice* fb = nullptr;

	bool useRayQuery = true;

	LevelMesh NullMesh;
	LevelMesh* Mesh = nullptr;

	std::unique_ptr<VulkanBuffer> VertexBuffer;
	std::unique_ptr<VulkanBuffer> IndexBuffer;
	std::unique_ptr<VulkanBuffer> SurfaceIndexBuffer;
	std::unique_ptr<VulkanBuffer> SurfaceBuffer;
	std::unique_ptr<VulkanBuffer> PortalBuffer;

	std::unique_ptr<VulkanBuffer> NodeBuffer;

	TArray<SurfaceVertex> Vertices;
	static const int MaxDynamicVertices = 100'000;
	static const int MaxDynamicIndexes = 100'000;
	static const int MaxDynamicSurfaces = 100'000;
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
};
