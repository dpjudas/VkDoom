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

#include "vk_levelmesh.h"
#include "zvulkan/vulkanbuilders.h"
#include "vulkan/vk_renderdevice.h"
#include "vulkan/commands/vk_commandbuffer.h"
#include "hw_levelmesh.h"
#include "hw_material.h"
#include "texturemanager.h"

VkLevelMesh::VkLevelMesh(VulkanRenderDevice* fb) : fb(fb)
{
	useRayQuery = fb->GetDevice()->SupportsExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME) && fb->GetDevice()->PhysicalDevice.Features.RayQuery.rayQuery;

	SetLevelMesh(nullptr);
}

void VkLevelMesh::SetLevelMesh(LevelMesh* mesh)
{
	if (!mesh)
		mesh = &NullMesh;

	Mesh = mesh;
	CreateVulkanObjects();
}

void VkLevelMesh::Reset()
{
	auto deletelist = fb->GetCommands()->DrawDeleteList.get();
	deletelist->Add(std::move(VertexBuffer));
	deletelist->Add(std::move(UniformIndexBuffer));
	deletelist->Add(std::move(IndexBuffer));
	deletelist->Add(std::move(NodeBuffer));
	deletelist->Add(std::move(SurfaceBuffer));
	deletelist->Add(std::move(UniformsBuffer));
	deletelist->Add(std::move(SurfaceIndexBuffer));
	deletelist->Add(std::move(PortalBuffer));
	deletelist->Add(std::move(StaticBLAS.ScratchBuffer));
	deletelist->Add(std::move(StaticBLAS.AccelStructBuffer));
	deletelist->Add(std::move(StaticBLAS.AccelStruct));
	deletelist->Add(std::move(DynamicBLAS.ScratchBuffer));
	deletelist->Add(std::move(DynamicBLAS.AccelStructBuffer));
	deletelist->Add(std::move(DynamicBLAS.AccelStruct));
	deletelist->Add(std::move(TopLevelAS.TransferBuffer));
	deletelist->Add(std::move(TopLevelAS.InstanceBuffer));
	deletelist->Add(std::move(TopLevelAS.ScratchBuffer));
	deletelist->Add(std::move(TopLevelAS.AccelStructBuffer));
	deletelist->Add(std::move(TopLevelAS.AccelStruct));
}

void VkLevelMesh::CreateVulkanObjects()
{
	Reset();
	CreateBuffers();
	UploadMeshes(false);

	if (useRayQuery)
	{
		// Wait for uploads to finish
		PipelineBarrier()
			.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		CreateStaticBLAS();
		CreateDynamicBLAS();
		CreateTLASInstanceBuffer();

		UploadTLASInstanceBuffer();

		// Wait for bottom level builds to finish before using it as input to a toplevel accel structure. Also wait for the instance buffer upload to complete.
		PipelineBarrier()
			.AddMemory(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);

		CreateTopLevelAS();

		// Finish building the accel struct before using it from the shaders
		PipelineBarrier()
			.AddMemory(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}
	else
	{
		// Uploads must finish before we can read from the shaders
		PipelineBarrier()
			.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}
}

void VkLevelMesh::BeginFrame()
{
	UploadMeshes(true);

	if (useRayQuery)
	{
		// Wait for uploads to finish
		PipelineBarrier()
			.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		// Create a new dynamic BLAS

		// To do: we should reuse the buffers. However this requires we know when the command buffers are completely done with them first.
		auto deletelist = fb->GetCommands()->DrawDeleteList.get();
		deletelist->Add(std::move(DynamicBLAS.ScratchBuffer));
		deletelist->Add(std::move(DynamicBLAS.AccelStructBuffer));
		deletelist->Add(std::move(DynamicBLAS.AccelStruct));
		deletelist->Add(std::move(TopLevelAS.TransferBuffer));
		deletelist->Add(std::move(TopLevelAS.InstanceBuffer));

		DynamicBLAS = CreateBLAS(Mesh->DynamicMesh.get(), true, Mesh->StaticMesh->Mesh.Vertices.Size(), Mesh->StaticMesh->Mesh.Indexes.Size());

		CreateTLASInstanceBuffer();
		UploadTLASInstanceBuffer();

		// Wait for bottom level builds to finish before using it as input to a toplevel accel structure. Also wait for the instance buffer upload to complete.
		PipelineBarrier()
			.AddMemory(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);

		UpdateTopLevelAS();

		// Finish building the accel struct before using it from the shaders
		PipelineBarrier()
			.AddMemory(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}
	else
	{
		// Uploads must finish before we can read from the shaders
		PipelineBarrier()
			.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}
}

void VkLevelMesh::UploadMeshes(bool dynamicOnly)
{
	VkLevelMeshUploader uploader(this);
	uploader.Upload(dynamicOnly);
}

int VkLevelMesh::GetMaxVertexBufferSize()
{
	return Mesh->StaticMesh->Mesh.Vertices.Size() + MaxDynamicVertices;
}

int VkLevelMesh::GetMaxIndexBufferSize()
{
	return Mesh->StaticMesh->Mesh.Indexes.Size() + MaxDynamicIndexes;
}

int VkLevelMesh::GetMaxNodeBufferSize()
{
	return (int)Mesh->StaticMesh->Collision->get_nodes().size() + MaxDynamicNodes + 1; // + 1 for the merge root node
}

int VkLevelMesh::GetMaxSurfaceBufferSize()
{
	return Mesh->StaticMesh->GetSurfaceCount() + MaxDynamicSurfaces;
}

int VkLevelMesh::GetMaxUniformsBufferSize()
{
	return Mesh->StaticMesh->Mesh.Uniforms.Size() + MaxDynamicUniforms;
}

int VkLevelMesh::GetMaxSurfaceIndexBufferSize()
{
	return Mesh->StaticMesh->Mesh.SurfaceIndexes.Size() + MaxDynamicSurfaceIndexes;
}

void VkLevelMesh::CreateBuffers()
{
	VertexBuffer = BufferBuilder()
		.Usage(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			(useRayQuery ?
				VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0) |
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		.Size(GetMaxVertexBufferSize() * sizeof(FFlatVertex))
		.DebugName("VertexBuffer")
		.Create(fb->GetDevice());

	UniformIndexBuffer = BufferBuilder()
		.Usage(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(GetMaxVertexBufferSize() * sizeof(int))
		.DebugName("UniformIndexes")
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

	UniformsBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(GetMaxUniformsBufferSize() * sizeof(SurfaceUniforms))
		.DebugName("SurfaceUniformsBuffer")
		.Create(fb->GetDevice());

	PortalBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(Mesh->Portals.Size() * sizeof(PortalInfo))
		.DebugName("PortalBuffer")
		.Create(fb->GetDevice());
}

VkLevelMesh::BLAS VkLevelMesh::CreateBLAS(LevelSubmesh* submesh, bool preferFastBuild, int vertexOffset, int indexOffset)
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
	accelStructBLDesc.geometry.triangles.vertexStride = sizeof(FFlatVertex);
	accelStructBLDesc.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	accelStructBLDesc.geometry.triangles.indexData.deviceAddress = IndexBuffer->GetDeviceAddress() + indexOffset * sizeof(uint32_t);
	accelStructBLDesc.geometry.triangles.maxVertex = vertexOffset + submesh->Mesh.Vertices.Size() - 1;

	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildInfo.flags = preferFastBuild ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.ppGeometries = geometries;

	uint32_t maxPrimitiveCount = submesh->Mesh.Indexes.Size() / 3;

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

	return blas;
}

void VkLevelMesh::CreateStaticBLAS()
{
	StaticBLAS = CreateBLAS(Mesh->StaticMesh.get(), false, 0, 0);
}

void VkLevelMesh::CreateDynamicBLAS()
{
	DynamicBLAS = CreateBLAS(Mesh->DynamicMesh.get(), true, Mesh->StaticMesh->Mesh.Vertices.Size(), Mesh->StaticMesh->Mesh.Indexes.Size());
}

void VkLevelMesh::CreateTLASInstanceBuffer()
{
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
}

void VkLevelMesh::CreateTopLevelAS()
{
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
}

void VkLevelMesh::UpdateTopLevelAS()
{
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
}

void VkLevelMesh::UploadTLASInstanceBuffer()
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
}

/////////////////////////////////////////////////////////////////////////////

VkLevelMeshUploader::VkLevelMeshUploader(VkLevelMesh* mesh) : Mesh(mesh)
{
}

void VkLevelMeshUploader::Upload(bool dynamicOnly)
{
	UpdateSizes();
	UpdateLocations();

	start = dynamicOnly;
	end = locations.Size();

	size_t transferBufferSize = GetTransferSize();
	if (transferBufferSize == 0)
		return;

	BeginTransfer(transferBufferSize);

	UploadNodes();
	UploadVertices();
	UploadUniformIndexes();
	UploadIndexes();
	UploadSurfaceIndexes();
	UploadSurfaces();
	UploadUniforms();
	UploadPortals();

	EndTransfer(transferBufferSize);
}

void VkLevelMeshUploader::BeginTransfer(size_t transferBufferSize)
{
	cmdbuffer = Mesh->fb->GetCommands()->GetTransferCommands();
	transferBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.Size(transferBufferSize)
		.DebugName("UploadMeshes")
		.Create(Mesh->fb->GetDevice());

	data = (uint8_t*)transferBuffer->Map(0, transferBufferSize);
	datapos = 0;
}

void VkLevelMeshUploader::EndTransfer(size_t transferBufferSize)
{
	assert(datapos == transferBufferSize);

	transferBuffer->Unmap();
	Mesh->fb->GetCommands()->TransferDeleteList->Add(std::move(transferBuffer));
}

void VkLevelMeshUploader::UploadNodes()
{
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
		nodesHeader.root = locations[1].Node.Offset + locations[1].Node.Size;

		CollisionNode info;
		info.center = bbox.Center;
		info.extents = bbox.Extents;
		info.left = locations[0].Node.Offset + root0;
		info.right = locations[1].Node.Offset + root1;
		info.element_index = -1;

		*((CollisionNodeBufferHeader*)(data + datapos)) = nodesHeader;
		*((CollisionNode*)(data + datapos + sizeof(CollisionNodeBufferHeader))) = info;

		cmdbuffer->copyBuffer(transferBuffer.get(), Mesh->NodeBuffer.get(), datapos, 0, sizeof(CollisionNodeBufferHeader));
		cmdbuffer->copyBuffer(transferBuffer.get(), Mesh->NodeBuffer.get(), datapos + sizeof(CollisionNodeBufferHeader), sizeof(CollisionNodeBufferHeader) + nodesHeader.root * sizeof(CollisionNode), sizeof(CollisionNode));
	}
	else // second submesh is empty, just point the header at the first one
	{
		CollisionNodeBufferHeader nodesHeader;
		nodesHeader.root = locations[0].Submesh->Collision->get_root();

		*((CollisionNodeBufferHeader*)(data + datapos)) = nodesHeader;
		cmdbuffer->copyBuffer(transferBuffer.get(), Mesh->NodeBuffer.get(), datapos, 0, sizeof(CollisionNodeBufferHeader));
	}
	datapos += sizeof(CollisionNodeBufferHeader) + sizeof(CollisionNode);

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
			info.left = node.left != -1 ? node.left + cur.Node.Offset : -1;
			info.right = node.right != -1 ? node.right + cur.Node.Offset : -1;
			info.element_index = node.element_index != -1 ? node.element_index + cur.Index.Offset : -1;
			*(nodes++) = info;
		}

		size_t copysize = submesh->Collision->get_nodes().size() * sizeof(CollisionNode);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), Mesh->NodeBuffer.get(), datapos, +sizeof(CollisionNodeBufferHeader) + cur.Node.Offset * sizeof(CollisionNode), copysize);
		datapos += copysize;
	}
}

void VkLevelMeshUploader::UploadVertices()
{
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		auto submesh = cur.Submesh;

		size_t copysize = submesh->Mesh.Vertices.Size() * sizeof(FFlatVertex);
		memcpy(data + datapos, submesh->Mesh.Vertices.Data(), copysize);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), Mesh->VertexBuffer.get(), datapos, cur.Vertex.Offset * sizeof(FFlatVertex), copysize);
		datapos += copysize;
	}
}

void VkLevelMeshUploader::UploadUniformIndexes()
{
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		auto submesh = cur.Submesh;

		size_t copysize = submesh->Mesh.UniformIndexes.Size() * sizeof(int);
		memcpy(data + datapos, submesh->Mesh.UniformIndexes.Data(), copysize);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), Mesh->UniformIndexBuffer.get(), datapos, cur.UniformIndexes.Offset * sizeof(int), copysize);
		datapos += copysize;
	}
}

void VkLevelMeshUploader::UploadIndexes()
{
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		auto submesh = cur.Submesh;

		uint32_t* indexes = (uint32_t*)(data + datapos);
		for (int j = 0, count = submesh->Mesh.Indexes.Size(); j < count; ++j)
			*(indexes++) = cur.Vertex.Offset + submesh->Mesh.Indexes[j];

		size_t copysize = submesh->Mesh.Indexes.Size() * sizeof(uint32_t);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), Mesh->IndexBuffer.get(), datapos, cur.Index.Offset * sizeof(uint32_t), copysize);
		datapos += copysize;
	}
}

void VkLevelMeshUploader::UploadSurfaceIndexes()
{
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		auto submesh = cur.Submesh;

		int* indexes = (int*)(data + datapos);
		for (int j = 0, count = submesh->Mesh.SurfaceIndexes.Size(); j < count; ++j)
			*(indexes++) = cur.SurfaceIndex.Offset + submesh->Mesh.SurfaceIndexes[j];

		size_t copysize = submesh->Mesh.SurfaceIndexes.Size() * sizeof(int);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), Mesh->SurfaceIndexBuffer.get(), datapos, cur.SurfaceIndex.Offset * sizeof(int), copysize);
		datapos += copysize;
	}
}

void VkLevelMeshUploader::UploadSurfaces()
{
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		auto submesh = cur.Submesh;

		SurfaceInfo* surfaces = (SurfaceInfo*)(data + datapos);
		for (int j = 0, count = submesh->GetSurfaceCount(); j < count; ++j)
		{
			LevelMeshSurface* surface = submesh->GetSurface(j);

			SurfaceInfo info;
			info.Normal = FVector3(surface->Plane.X, surface->Plane.Z, surface->Plane.Y);
			info.PortalIndex = surface->PortalIndex;
			info.Sky = surface->IsSky;
			info.Alpha = surface->Alpha;
			if (surface->Texture)
			{
				auto mat = FMaterial::ValidateTexture(surface->Texture, 0);
				info.TextureIndex = Mesh->fb->GetBindlessTextureIndex(mat, CLAMP_NONE, 0);
			}
			else
			{
				info.TextureIndex = 0;
			}

			*(surfaces++) = info;
		}

		size_t copysize = submesh->GetSurfaceCount() * sizeof(SurfaceInfo);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), Mesh->SurfaceBuffer.get(), datapos, cur.Surface.Offset * sizeof(SurfaceInfo), copysize);
		datapos += copysize;
	}
}

void VkLevelMeshUploader::UploadUniforms()
{
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		auto submesh = cur.Submesh;

		for (int j = 0, count = submesh->Mesh.Uniforms.Size(); j < count; j++)
		{
			auto& surfaceUniforms = submesh->Mesh.Uniforms[j];
			auto& material = submesh->Mesh.Materials[j];
			if (material.mMaterial)
			{
				auto source = material.mMaterial->Source();
				surfaceUniforms.uSpecularMaterial = { source->GetGlossiness(), source->GetSpecularLevel() };
				surfaceUniforms.uTextureIndex = Mesh->fb->GetBindlessTextureIndex(material.mMaterial, material.mClampMode, material.mTranslation);
			}
			else
			{
				surfaceUniforms.uTextureIndex = 0;
			}
		}

		SurfaceUniforms* uniforms = (SurfaceUniforms*)(data + datapos);
		size_t copysize = submesh->Mesh.Uniforms.Size() * sizeof(SurfaceUniforms);
		memcpy(uniforms, submesh->Mesh.Uniforms.Data(), copysize);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), Mesh->UniformsBuffer.get(), datapos, cur.Uniforms.Offset * sizeof(SurfaceUniforms), copysize);
		datapos += copysize;
	}
}

void VkLevelMeshUploader::UploadPortals()
{
	if (start == 0)
	{
		PortalInfo* portals = (PortalInfo*)(data + datapos);
		for (auto& portal : Mesh->Mesh->Portals)
		{
			PortalInfo info;
			info.transformation = portal.transformation;
			*(portals++) = info;
		}

		size_t copysize = Mesh->Mesh->Portals.Size() * sizeof(PortalInfo);
		if (copysize > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), Mesh->PortalBuffer.get(), datapos, 0, copysize);
		datapos += copysize;
	}
}

void VkLevelMeshUploader::UpdateSizes()
{
	for (LevelSubmesh* submesh : { Mesh->GetMesh()->StaticMesh.get(), Mesh->GetMesh()->DynamicMesh.get() })
	{
		SubmeshBufferLocation location;
		location.Submesh = submesh;
		location.Vertex.Size = submesh->Mesh.Vertices.Size();
		location.Index.Size = submesh->Mesh.Indexes.Size();
		location.Node.Size = (int)submesh->Collision->get_nodes().size();
		location.SurfaceIndex.Size = submesh->Mesh.SurfaceIndexes.Size();
		location.Surface.Size = submesh->GetSurfaceCount();
		location.UniformIndexes.Size = submesh->Mesh.UniformIndexes.Size();
		location.Uniforms.Size = submesh->Mesh.Uniforms.Size();
		locations.Push(location);
	}
}

void VkLevelMeshUploader::UpdateLocations()
{
	for (unsigned int i = 1, count = locations.Size(); i < count; i++)
	{
		const SubmeshBufferLocation& prev = locations[i - 1];
		SubmeshBufferLocation& cur = locations[i];
		cur.Vertex.Offset = prev.Vertex.Offset + prev.Vertex.Size;
		cur.Index.Offset = prev.Index.Offset + prev.Index.Size;
		cur.Node.Offset = prev.Node.Offset + prev.Node.Size;
		cur.SurfaceIndex.Offset = prev.SurfaceIndex.Offset + prev.SurfaceIndex.Size;
		cur.Surface.Offset = prev.Surface.Offset + prev.Surface.Size;
		cur.UniformIndexes.Offset = prev.UniformIndexes.Offset + prev.UniformIndexes.Size;
		cur.Uniforms.Offset = prev.Uniforms.Offset + prev.Uniforms.Size;

		if (
			cur.Vertex.Offset + cur.Vertex.Size > Mesh->GetMaxVertexBufferSize() ||
			cur.Index.Offset + cur.Index.Size > Mesh->GetMaxIndexBufferSize() ||
			cur.Node.Offset + cur.Node.Size > Mesh->GetMaxNodeBufferSize() ||
			cur.SurfaceIndex.Offset + cur.SurfaceIndex.Size > Mesh->GetMaxSurfaceIndexBufferSize() ||
			cur.Surface.Offset + cur.Surface.Size > Mesh->GetMaxSurfaceBufferSize() ||
			cur.UniformIndexes.Offset + cur.UniformIndexes.Size > Mesh->GetMaxVertexBufferSize() ||
			cur.Uniforms.Offset + cur.Uniforms.Size > Mesh->GetMaxUniformsBufferSize())
		{
			I_FatalError("Dynamic accel struct buffers are too small!");
		}
	}
}

size_t VkLevelMeshUploader::GetTransferSize()
{
	// Figure out how much memory we need to transfer it to the GPU
	size_t transferBufferSize = sizeof(CollisionNodeBufferHeader) + sizeof(CollisionNode);
	for (unsigned int i = start; i < end; i++)
	{
		const SubmeshBufferLocation& cur = locations[i];
		transferBufferSize += cur.Submesh->Mesh.Vertices.Size() * sizeof(FFlatVertex);
		transferBufferSize += cur.Submesh->Mesh.UniformIndexes.Size() * sizeof(int);
		transferBufferSize += cur.Submesh->Mesh.Indexes.Size() * sizeof(uint32_t);
		transferBufferSize += cur.Submesh->Collision->get_nodes().size() * sizeof(CollisionNode);
		transferBufferSize += cur.Submesh->Mesh.SurfaceIndexes.Size() * sizeof(int);
		transferBufferSize += cur.Submesh->GetSurfaceCount() * sizeof(SurfaceInfo);
		transferBufferSize += cur.Submesh->Mesh.Uniforms.Size() * sizeof(SurfaceUniforms);
	}
	if (start == 0)
		transferBufferSize += Mesh->GetMesh()->Portals.Size() * sizeof(PortalInfo);
	return transferBufferSize;
}
