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

#include "vk_raytrace.h"
#include "zvulkan/vulkanbuilders.h"
#include "vulkan/vk_renderdevice.h"
#include "vulkan/commands/vk_commandbuffer.h"
#include "hw_levelmesh.h"
#include "hw_material.h"
#include "texturemanager.h"

VkRaytrace::VkRaytrace(VulkanRenderDevice* fb) : fb(fb)
{
	useRayQuery = fb->GetDevice()->SupportsExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME) && fb->GetDevice()->PhysicalDevice.Features.RayQuery.rayQuery;

	SetLevelMesh(nullptr);
}

void VkRaytrace::SetLevelMesh(LevelMesh* mesh)
{
	if (!mesh)
		mesh = &NullMesh;

	Reset();
	Mesh = mesh;
	CreateVulkanObjects();
}

void VkRaytrace::Reset()
{
	auto deletelist = fb->GetCommands()->DrawDeleteList.get();
	deletelist->Add(std::move(VertexBuffer));
	deletelist->Add(std::move(IndexBuffer));
	deletelist->Add(std::move(NodeBuffer));
	deletelist->Add(std::move(SurfaceBuffer));
	deletelist->Add(std::move(SurfaceIndexBuffer));
	deletelist->Add(std::move(PortalBuffer));
	deletelist->Add(std::move(StaticBLAS.ScratchBuffer));
	deletelist->Add(std::move(StaticBLAS.AccelStructBuffer));
	deletelist->Add(std::move(StaticBLAS.AccelStruct));
	deletelist->Add(std::move(DynamicBLAS.ScratchBuffer));
	deletelist->Add(std::move(DynamicBLAS.AccelStructBuffer));
	deletelist->Add(std::move(DynamicBLAS.AccelStruct));
	deletelist->Add(std::move(TopLevelAS.TransferBuffer));
	deletelist->Add(std::move(TopLevelAS.ScratchBuffer));
	deletelist->Add(std::move(TopLevelAS.InstanceBuffer));
	deletelist->Add(std::move(TopLevelAS.AccelStructBuffer));
	deletelist->Add(std::move(TopLevelAS.AccelStruct));
}

void VkRaytrace::CreateVulkanObjects()
{
	CreateBuffers();
	UploadMeshes(false);
	if (useRayQuery)
	{
		CreateStaticBLAS();
		CreateDynamicBLAS();
		CreateTopLevelAS();
	}
}

void VkRaytrace::BeginFrame()
{
	UploadMeshes(true);
	if (useRayQuery)
	{
		UpdateDynamicBLAS();
		UpdateTopLevelAS();
	}
}

void VkRaytrace::UploadMeshes(bool dynamicOnly)
{
	TArray<SubmeshBufferLocation> locations(2);

	// Find submesh buffer sizes
	for (LevelSubmesh* submesh : { Mesh->StaticMesh.get(), Mesh->DynamicMesh.get() })
	{
		SubmeshBufferLocation location;
		location.Submesh = submesh;
		location.VertexSize = submesh->MeshVertices.Size();
		location.IndexSize = submesh->MeshElements.Size();
		location.NodeSize = (int)submesh->Collision->get_nodes().size();
		location.SurfaceIndexSize = submesh->MeshSurfaceIndexes.Size();
		location.SurfaceSize = submesh->GetSurfaceCount();
		locations.Push(location);
	}

	// Find submesh locations in buffers
	for (unsigned int i = 1, count = locations.Size(); i < count; i++)
	{
		const SubmeshBufferLocation& prev = locations[i - 1];
		SubmeshBufferLocation& cur = locations[i];
		cur.VertexOffset = prev.VertexOffset + prev.VertexSize;
		cur.IndexOffset = prev.IndexOffset + prev.IndexSize;
		cur.NodeOffset = prev.NodeOffset + prev.NodeSize;
		cur.SurfaceIndexOffset = prev.SurfaceIndexOffset + prev.SurfaceIndexSize;
		cur.SurfaceOffset = prev.SurfaceOffset + prev.SurfaceSize;

		if (
			cur.VertexOffset + cur.VertexSize > GetMaxVertexBufferSize() ||
			cur.IndexOffset + cur.IndexSize > GetMaxIndexBufferSize() ||
			cur.NodeOffset + cur.NodeSize > GetMaxNodeBufferSize() ||
			cur.SurfaceOffset + cur.SurfaceSize > GetMaxSurfaceBufferSize() ||
			cur.SurfaceIndexOffset + cur.SurfaceIndexSize > GetMaxSurfaceIndexBufferSize())
		{
			I_FatalError("Dynamic accel struct buffers are too small!");
		}
	}

	unsigned int start = dynamicOnly;
	unsigned int end = locations.Size();

	// Figure out how much memory we need to transfer it to the GPU
	size_t transferBufferSize = sizeof(CollisionNodeBufferHeader) + sizeof(CollisionNode);
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		transferBufferSize += cur.Submesh->MeshVertices.Size() * sizeof(SurfaceVertex);
		transferBufferSize += cur.Submesh->MeshElements.Size() * sizeof(uint32_t);
		transferBufferSize += cur.Submesh->Collision->get_nodes().size() * sizeof(CollisionNode);
		transferBufferSize += cur.Submesh->MeshSurfaceIndexes.Size() * sizeof(int);
		transferBufferSize += cur.Submesh->GetSurfaceCount() * sizeof(SurfaceInfo);
	}
	if (!dynamicOnly)
		transferBufferSize += Mesh->StaticMesh->Portals.Size() * sizeof(PortalInfo);

	// Begin the transfer
	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();
	auto transferBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.Size(transferBufferSize)
		.DebugName("UploadMeshes")
		.Create(fb->GetDevice());

	uint8_t* data = (uint8_t*)transferBuffer->Map(0, transferBufferSize);
	size_t datapos = 0;

	// Copy node buffer header and create a root node that merges the static and dynamic AABB trees
	if (locations[1].Submesh->Collision->get_root() != -1)
	{
		int root0 = locations[0].Submesh->Collision->get_root();
		int root1 = locations[1].Submesh->Collision->get_root();
		const auto& node0 = locations[0].Submesh->Collision->get_nodes()[root0];
		const auto& node1 = locations[1].Submesh->Collision->get_nodes()[root1];

		FVector3 aabbMin(std::min(node0.aabb.min.X, node1.aabb.min.X), std::min(node0.aabb.min.Y, node1.aabb.min.Y), std::min(node0.aabb.min.Z, node1.aabb.min.Z));
		FVector3 aabbMax(std::max(node0.aabb.max.X, node1.aabb.max.X), std::max(node0.aabb.max.Y, node1.aabb.max.Y), std::max(node0.aabb.max.Z, node1.aabb.max.Z));
		CollisionBBox bbox(aabbMin, aabbMax);

		CollisionNodeBufferHeader nodesHeader;
		nodesHeader.root = locations[1].NodeOffset + locations[1].NodeSize;

		CollisionNode info;
		info.center = bbox.Center;
		info.extents = bbox.Extents;
		info.left = locations[0].NodeOffset + root0;
		info.right = locations[1].NodeOffset + root1;
		info.element_index = -1;

		*((CollisionNodeBufferHeader*)(data + datapos)) = nodesHeader;
		*((CollisionNode*)(data + datapos + sizeof(CollisionNodeBufferHeader))) = info;

		cmdbuffer->copyBuffer(transferBuffer.get(), NodeBuffer.get(), datapos, 0, sizeof(CollisionNodeBufferHeader));
		cmdbuffer->copyBuffer(transferBuffer.get(), NodeBuffer.get(), datapos + sizeof(CollisionNodeBufferHeader), sizeof(CollisionNodeBufferHeader) + nodesHeader.root * sizeof(CollisionNode), sizeof(CollisionNode));
	}
	else // second submesh is empty, just point the header at the first one
	{
		CollisionNodeBufferHeader nodesHeader;
		nodesHeader.root = locations[0].Submesh->Collision->get_root();

		*((CollisionNodeBufferHeader*)(data + datapos)) = nodesHeader;
		cmdbuffer->copyBuffer(transferBuffer.get(), NodeBuffer.get(), datapos, 0, sizeof(CollisionNodeBufferHeader));
	}
	datapos += sizeof(CollisionNodeBufferHeader) + sizeof(CollisionNode);

	// Copy vertices
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		auto submesh = cur.Submesh;

		SurfaceVertex* vertices = (SurfaceVertex*)(data + datapos);
		for (int j = 0, count = submesh->MeshVertices.Size(); j < count; ++j)
			*(vertices++) = { { submesh->MeshVertices[j], 1.0f }, submesh->MeshVertexUVs[j], float(j), j + 10000.0f };

		size_t copysize = submesh->MeshVertices.Size() * sizeof(SurfaceVertex);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), VertexBuffer.get(), datapos, cur.VertexOffset * sizeof(SurfaceVertex), copysize);
		datapos += copysize;
	}

	// Copy indexes
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		auto submesh = cur.Submesh;

		uint32_t* indexes = (uint32_t*)(data + datapos);
		for (int j = 0, count = submesh->MeshElements.Size(); j < count; ++j)
			*(indexes++) = cur.VertexOffset + submesh->MeshElements[j];

		size_t copysize = submesh->MeshElements.Size() * sizeof(uint32_t);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), IndexBuffer.get(), datapos, cur.IndexOffset * sizeof(uint32_t), copysize);
		datapos += copysize;
	}

	// Copy collision nodes
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		auto submesh = cur.Submesh;

		CollisionNode* nodes = (CollisionNode*)(data + datapos);
		for (auto& node : submesh->Collision->get_nodes())
		{
			CollisionNode info;
			info.center = node.aabb.Center;
			info.extents = node.aabb.Extents;
			info.left = node.left != -1 ? node.left + cur.NodeOffset : -1;
			info.right = node.right != -1 ? node.right + cur.NodeOffset : -1;
			info.element_index = node.element_index != -1 ? node.element_index + cur.IndexOffset : -1;
			*(nodes++) = info;
		}

		size_t copysize = submesh->Collision->get_nodes().size() * sizeof(CollisionNode);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), NodeBuffer.get(), datapos, +sizeof(CollisionNodeBufferHeader) + cur.NodeOffset * sizeof(CollisionNode), copysize);
		datapos += copysize;
	}

	// Copy surface indexes
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		auto submesh = cur.Submesh;

		int* indexes = (int*)(data + datapos);
		for (int j = 0, count = submesh->MeshSurfaceIndexes.Size(); j < count; ++j)
			*(indexes++) = cur.SurfaceIndexOffset + submesh->MeshSurfaceIndexes[j];

		size_t copysize = submesh->MeshSurfaceIndexes.Size() * sizeof(int);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), SurfaceIndexBuffer.get(), datapos, cur.SurfaceIndexOffset * sizeof(int), copysize);
		datapos += copysize;
	}

	// Copy surfaces
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		auto submesh = cur.Submesh;

		SurfaceInfo* surfaces = (SurfaceInfo*)(data + datapos);
		for (int j = 0, count = submesh->GetSurfaceCount(); j < count; ++j)
		{
			LevelMeshSurface* surface = submesh->GetSurface(j);

			SurfaceInfo info;
			info.Normal = surface->plane.XYZ();
			info.PortalIndex = surface->portalIndex;
			info.SamplingDistance = (float)surface->sampleDimension;
			info.Sky = surface->bSky;
			if (surface->texture.isValid())
			{
				auto mat = FMaterial::ValidateTexture(TexMan.GetGameTexture(surface->texture), 0);
				info.TextureIndex = fb->GetBindlessTextureIndex(mat, CLAMP_NONE, 0);
			}
			else
			{
				info.TextureIndex = -1;
			}
			info.Alpha = surface->alpha;

			*(surfaces++) = info;
		}

		size_t copysize = submesh->GetSurfaceCount() * sizeof(SurfaceInfo);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), SurfaceBuffer.get(), datapos, cur.SurfaceOffset * sizeof(SurfaceInfo), copysize);
		datapos += copysize;
	}

	// Copy portals
	if (!dynamicOnly)
	{
		PortalInfo* portals = (PortalInfo*)(data + datapos);
		for (auto& portal : Mesh->StaticMesh->Portals)
		{
			PortalInfo info;
			info.transformation = portal.transformation;
			*(portals++) = info;
		}

		size_t copysize = Mesh->StaticMesh->Portals.Size() * sizeof(PortalInfo);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), PortalBuffer.get(), datapos, 0, copysize);
		datapos += copysize;
	}

	assert(datapos == transferBufferSize);

	// End the transfer
	transferBuffer->Unmap();

	fb->GetCommands()->TransferDeleteList->Add(std::move(transferBuffer));

	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, useRayQuery ? VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR : VK_ACCESS_SHADER_READ_BIT)
		.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, useRayQuery ? VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

int VkRaytrace::GetMaxVertexBufferSize()
{
	return Mesh->StaticMesh->MeshVertices.Size() + MaxDynamicVertices;
}

int VkRaytrace::GetMaxIndexBufferSize()
{
	return Mesh->StaticMesh->MeshElements.Size() + MaxDynamicIndexes;
}

int VkRaytrace::GetMaxNodeBufferSize()
{
	return (int)Mesh->StaticMesh->Collision->get_nodes().size() + MaxDynamicNodes + 1; // + 1 for the merge root node
}

int VkRaytrace::GetMaxSurfaceBufferSize()
{
	return Mesh->StaticMesh->GetSurfaceCount() + MaxDynamicSurfaces;
}

int VkRaytrace::GetMaxSurfaceIndexBufferSize()
{
	return Mesh->StaticMesh->MeshSurfaceIndexes.Size() + MaxDynamicSurfaceIndexes;
}

void VkRaytrace::CreateBuffers()
{
	VertexBuffer = BufferBuilder()
		.Usage(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			(useRayQuery ?
				VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0) |
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		.Size(GetMaxVertexBufferSize() * sizeof(SurfaceVertex))
		.DebugName("VertexBuffer")
		.Create(fb->GetDevice());

	IndexBuffer = BufferBuilder()
		.Usage(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			(useRayQuery ?
				VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0) |
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		.Size((size_t)GetMaxIndexBufferSize() * sizeof(uint32_t))
		.DebugName("IndexBuffer")
		.Create(fb->GetDevice());

	NodeBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(sizeof(CollisionNodeBufferHeader) + GetMaxNodeBufferSize() * sizeof(CollisionNode))
		.DebugName("NodeBuffer")
		.Create(fb->GetDevice());

	SurfaceIndexBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(GetMaxSurfaceIndexBufferSize() * sizeof(int))
		.DebugName("SurfaceBuffer")
		.Create(fb->GetDevice());

	SurfaceBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(GetMaxSurfaceBufferSize() * sizeof(SurfaceInfo))
		.DebugName("SurfaceBuffer")
		.Create(fb->GetDevice());

	PortalBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(Mesh->StaticMesh->Portals.Size() * sizeof(PortalInfo))
		.DebugName("PortalBuffer")
		.Create(fb->GetDevice());
}

VkRaytrace::BLAS VkRaytrace::CreateBLAS(LevelSubmesh* submesh, bool preferFastBuild, int vertexOffset, int indexOffset)
{
	BLAS blas;

	VkAccelerationStructureBuildGeometryInfoKHR buildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureGeometryKHR accelStructBLDesc = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	VkAccelerationStructureGeometryKHR* geometries[] = { &accelStructBLDesc };

	accelStructBLDesc.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	accelStructBLDesc.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	accelStructBLDesc.geometry.triangles = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
	accelStructBLDesc.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
	accelStructBLDesc.geometry.triangles.vertexData.deviceAddress = VertexBuffer->GetDeviceAddress();
	accelStructBLDesc.geometry.triangles.vertexStride = sizeof(SurfaceVertex);
	accelStructBLDesc.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	accelStructBLDesc.geometry.triangles.indexData.deviceAddress = IndexBuffer->GetDeviceAddress() + indexOffset * sizeof(uint32_t);
	accelStructBLDesc.geometry.triangles.maxVertex = vertexOffset + submesh->MeshVertices.Size() - 1;

	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildInfo.flags = preferFastBuild ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.ppGeometries = geometries;

	uint32_t maxPrimitiveCount = submesh->MeshElements.Size() / 3;

	VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	vkGetAccelerationStructureBuildSizesKHR(fb->GetDevice()->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &maxPrimitiveCount, &sizeInfo);

	blas.AccelStructBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
		.Size(sizeInfo.accelerationStructureSize)
		.DebugName("BLAS.AccelStructBuffer")
		.Create(fb->GetDevice());

	blas.AccelStruct = AccelerationStructureBuilder()
		.Type(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)
		.Buffer(blas.AccelStructBuffer.get(), sizeInfo.accelerationStructureSize)
		.DebugName("BLAS.AccelStruct")
		.Create(fb->GetDevice());

	blas.ScratchBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		.Size(sizeInfo.buildScratchSize)
		.MinAlignment(fb->GetDevice()->PhysicalDevice.Properties.AccelerationStructure.minAccelerationStructureScratchOffsetAlignment)
		.DebugName("BLAS.ScratchBuffer")
		.Create(fb->GetDevice());

	buildInfo.dstAccelerationStructure = blas.AccelStruct->accelstruct;
	buildInfo.scratchData.deviceAddress = blas.ScratchBuffer->GetDeviceAddress();

	VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
	VkAccelerationStructureBuildRangeInfoKHR* rangeInfos[] = { &rangeInfo };
	rangeInfo.primitiveCount = maxPrimitiveCount;

	fb->GetCommands()->GetTransferCommands()->buildAccelerationStructures(1, &buildInfo, rangeInfos);

	// Finish building before using it as input to a toplevel accel structure
	PipelineBarrier()
		.AddMemory(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR)
		.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);

	return blas;
}

void VkRaytrace::CreateStaticBLAS()
{
	StaticBLAS = CreateBLAS(Mesh->StaticMesh.get(), false, 0, 0);
}

void VkRaytrace::CreateDynamicBLAS()
{
	DynamicBLAS = CreateBLAS(Mesh->DynamicMesh.get(), true, Mesh->StaticMesh->MeshVertices.Size(), Mesh->StaticMesh->MeshElements.Size());
}

void VkRaytrace::CreateTopLevelAS()
{
	auto deletelist = fb->GetCommands()->DrawDeleteList.get();
	deletelist->Add(std::move(TopLevelAS.TransferBuffer));
	deletelist->Add(std::move(TopLevelAS.ScratchBuffer));
	deletelist->Add(std::move(TopLevelAS.InstanceBuffer));
	deletelist->Add(std::move(TopLevelAS.AccelStructBuffer));
	deletelist->Add(std::move(TopLevelAS.AccelStruct));

	VkAccelerationStructureInstanceKHR instances[2] = {};
	instances[0].transform.matrix[0][0] = 1.0f;
	instances[0].transform.matrix[1][1] = 1.0f;
	instances[0].transform.matrix[2][2] = 1.0f;
	instances[0].mask = 0xff;
	instances[0].flags = 0;
	instances[0].accelerationStructureReference = StaticBLAS.AccelStruct->GetDeviceAddress();

	instances[1].transform.matrix[0][0] = 1.0f;
	instances[1].transform.matrix[1][1] = 1.0f;
	instances[1].transform.matrix[2][2] = 1.0f;
	instances[1].mask = 0xff;
	instances[1].flags = 0;
	instances[1].accelerationStructureReference = DynamicBLAS.AccelStruct->GetDeviceAddress();

	TopLevelAS.TransferBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.Size(sizeof(VkAccelerationStructureInstanceKHR) * 2)
		.DebugName("TopLevelAS.TransferBuffer")
		.Create(fb->GetDevice());

	TopLevelAS.InstanceBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(sizeof(VkAccelerationStructureInstanceKHR) * 2)
		.DebugName("TopLevelAS.InstanceBuffer")
		.Create(fb->GetDevice());

	auto data = (uint8_t*)TopLevelAS.TransferBuffer->Map(0, sizeof(VkAccelerationStructureInstanceKHR) * 2);
	memcpy(data, instances, sizeof(VkAccelerationStructureInstanceKHR) * 2);
	TopLevelAS.TransferBuffer->Unmap();

	fb->GetCommands()->GetTransferCommands()->copyBuffer(TopLevelAS.TransferBuffer.get(), TopLevelAS.InstanceBuffer.get());

	// Finish transfering before using it as input
	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR)
		.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);

	VkAccelerationStructureBuildGeometryInfoKHR buildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureGeometryKHR accelStructTLDesc = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	VkAccelerationStructureGeometryKHR* geometries[] = { &accelStructTLDesc };

	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.ppGeometries = geometries;

	accelStructTLDesc.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	accelStructTLDesc.geometry.instances = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
	accelStructTLDesc.geometry.instances.data.deviceAddress = TopLevelAS.InstanceBuffer->GetDeviceAddress();

	uint32_t maxInstanceCount = 2;

	VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	vkGetAccelerationStructureBuildSizesKHR(fb->GetDevice()->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &maxInstanceCount, &sizeInfo);

	TopLevelAS.AccelStructBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
		.Size(sizeInfo.accelerationStructureSize)
		.DebugName("TopLevelAS.AccelStructBuffer")
		.Create(fb->GetDevice());

	TopLevelAS.AccelStruct = AccelerationStructureBuilder()
		.Type(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR)
		.Buffer(TopLevelAS.AccelStructBuffer.get(), sizeInfo.accelerationStructureSize)
		.DebugName("TopLevelAS.AccelStruct")
		.Create(fb->GetDevice());

	TopLevelAS.ScratchBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		.Size(sizeInfo.buildScratchSize)
		.MinAlignment(fb->GetDevice()->PhysicalDevice.Properties.AccelerationStructure.minAccelerationStructureScratchOffsetAlignment)
		.DebugName("TopLevelAS.ScratchBuffer")
		.Create(fb->GetDevice());

	buildInfo.dstAccelerationStructure = TopLevelAS.AccelStruct->accelstruct;
	buildInfo.scratchData.deviceAddress = TopLevelAS.ScratchBuffer->GetDeviceAddress();

	VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
	VkAccelerationStructureBuildRangeInfoKHR* rangeInfos[] = { &rangeInfo };
	rangeInfo.primitiveCount = 2;

	fb->GetCommands()->GetTransferCommands()->buildAccelerationStructures(1, &buildInfo, rangeInfos);

	// Finish building the accel struct before using as input in a fragment shader
	PipelineBarrier()
		.AddMemory(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_SHADER_READ_BIT)
		.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void VkRaytrace::UpdateDynamicBLAS()
{
	// To do: should we reuse the buffers?

	auto deletelist = fb->GetCommands()->DrawDeleteList.get();
	deletelist->Add(std::move(DynamicBLAS.ScratchBuffer));
	deletelist->Add(std::move(DynamicBLAS.AccelStructBuffer));
	deletelist->Add(std::move(DynamicBLAS.AccelStruct));

	DynamicBLAS = CreateBLAS(Mesh->DynamicMesh.get(), true, Mesh->StaticMesh->MeshVertices.Size(), Mesh->StaticMesh->MeshElements.Size());
}

void VkRaytrace::UpdateTopLevelAS()
{
	VkAccelerationStructureInstanceKHR instances[2] = {};
	instances[0].transform.matrix[0][0] = 1.0f;
	instances[0].transform.matrix[1][1] = 1.0f;
	instances[0].transform.matrix[2][2] = 1.0f;
	instances[0].mask = 0xff;
	instances[0].flags = 0;
	instances[0].accelerationStructureReference = StaticBLAS.AccelStruct->GetDeviceAddress();

	instances[1].transform.matrix[0][0] = 1.0f;
	instances[1].transform.matrix[1][1] = 1.0f;
	instances[1].transform.matrix[2][2] = 1.0f;
	instances[1].mask = 0xff;
	instances[1].flags = 0;
	instances[1].accelerationStructureReference = DynamicBLAS.AccelStruct->GetDeviceAddress();

	auto data = (uint8_t*)TopLevelAS.TransferBuffer->Map(0, sizeof(VkAccelerationStructureInstanceKHR) * 2);
	memcpy(data, instances, sizeof(VkAccelerationStructureInstanceKHR) * 2);
	TopLevelAS.TransferBuffer->Unmap();

	fb->GetCommands()->GetTransferCommands()->copyBuffer(TopLevelAS.TransferBuffer.get(), TopLevelAS.InstanceBuffer.get());

	// Finish transfering before using it as input
	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR)
		.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);

	VkAccelerationStructureBuildGeometryInfoKHR buildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureGeometryKHR accelStructTLDesc = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	VkAccelerationStructureGeometryKHR* geometries[] = { &accelStructTLDesc };

	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.ppGeometries = geometries;

	accelStructTLDesc.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	accelStructTLDesc.geometry.instances = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
	accelStructTLDesc.geometry.instances.data.deviceAddress = TopLevelAS.InstanceBuffer->GetDeviceAddress();

	buildInfo.dstAccelerationStructure = TopLevelAS.AccelStruct->accelstruct;
	buildInfo.scratchData.deviceAddress = TopLevelAS.ScratchBuffer->GetDeviceAddress();

	VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
	VkAccelerationStructureBuildRangeInfoKHR* rangeInfos[] = { &rangeInfo };
	rangeInfo.primitiveCount = 2;

	fb->GetCommands()->GetTransferCommands()->buildAccelerationStructures(1, &buildInfo, rangeInfos);

	// Finish building the accel struct before using as input in a fragment shader
	PipelineBarrier()
		.AddMemory(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_SHADER_READ_BIT)
		.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

