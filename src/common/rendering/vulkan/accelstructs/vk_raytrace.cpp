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
	useRayQuery = fb->GetDevice()->SupportsExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);

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
	if (useRayQuery)
	{
		UploadStatic();
		UploadDynamic();
		CreateStaticBLAS();
		CreateDynamicBLAS();
		CreateTopLevelAS();
	}
}

void VkRaytrace::BeginFrame()
{
	if (useRayQuery)
	{
		UploadDynamic();
		UpdateDynamicBLAS();
		UpdateTopLevelAS();
	}
}

void VkRaytrace::UploadStatic()
{
	auto submesh = Mesh->StaticMesh.get();

	Vertices = TArray<SurfaceVertex>(GetMaxVertexBufferSize());

	for (int i = 0, count = submesh->MeshVertices.Size(); i < count; ++i)
		Vertices.Push({ { submesh->MeshVertices[i], 1.0f }, submesh->MeshVertexUVs[i], float(i), i + 10000.0f });

	CollisionNodeBufferHeader nodesHeader;
	nodesHeader.root = submesh->Collision->get_root();
	TArray<CollisionNode> nodes = CreateCollisionNodes(submesh);

	TArray<SurfaceInfo> surfaceInfo = CreateSurfaceInfo(submesh);

	TArray<PortalInfo> portalInfo;
	for (auto& portal : submesh->Portals)
	{
		PortalInfo info;
		info.transformation = portal.transformation;
		portalInfo.Push(info);
	}

	auto transferBuffer = BufferTransfer()
		.AddBuffer(VertexBuffer.get(), Vertices.Data(), Vertices.Size() * sizeof(SurfaceVertex))
		.AddBuffer(IndexBuffer.get(), submesh->MeshElements.Data(), (size_t)submesh->MeshElements.Size() * sizeof(uint32_t))
		.AddBuffer(NodeBuffer.get(), &nodesHeader, sizeof(CollisionNodeBufferHeader), nodes.Data(), nodes.Size() * sizeof(CollisionNode))
		.AddBuffer(SurfaceIndexBuffer.get(), submesh->MeshSurfaceIndexes.Data(), submesh->MeshSurfaceIndexes.Size() * sizeof(int))
		.AddBuffer(SurfaceBuffer.get(), surfaceInfo.Data(), surfaceInfo.Size() * sizeof(SurfaceInfo))
		.AddBuffer(PortalBuffer.get(), portalInfo.Data(), portalInfo.Size() * sizeof(PortalInfo))
		.Execute(fb->GetDevice(), fb->GetCommands()->GetTransferCommands());

	fb->GetCommands()->TransferDeleteList->Add(std::move(transferBuffer));

	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, useRayQuery ? VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR : VK_ACCESS_SHADER_READ_BIT)
		.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, useRayQuery ? VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void VkRaytrace::UploadDynamic()
{
	auto submesh = Mesh->DynamicMesh.get();

	Vertices.Resize(Mesh->StaticMesh->MeshVertices.Size());

	for (int i = 0, count = submesh->MeshVertices.Size(); i < count; ++i)
		Vertices.Push({ { submesh->MeshVertices[i], 1.0f }, submesh->MeshVertexUVs[i], float(i), i + 10000.0f });

	TArray<SurfaceInfo> surfaceInfo = CreateSurfaceInfo(submesh);

	auto transferBuffer = BufferTransfer()
		.AddBuffer(VertexBuffer.get(), Mesh->StaticMesh->MeshVertices.Size() * sizeof(SurfaceVertex), Vertices.Data() + Mesh->StaticMesh->MeshVertices.Size(), Vertices.Size() * sizeof(SurfaceVertex))
		.AddBuffer(IndexBuffer.get(), Mesh->StaticMesh->MeshElements.Size() * sizeof(uint32_t), submesh->MeshElements.Data(), (size_t)submesh->MeshElements.Size() * sizeof(uint32_t))
		.AddBuffer(SurfaceIndexBuffer.get(), Mesh->StaticMesh->MeshSurfaceIndexes.Size() * sizeof(int), submesh->MeshSurfaceIndexes.Data(), submesh->MeshSurfaceIndexes.Size() * sizeof(int))
		.AddBuffer(SurfaceBuffer.get(), Mesh->StaticMesh->GetSurfaceCount() * sizeof(SurfaceInfo), surfaceInfo.Data(), surfaceInfo.Size() * sizeof(SurfaceInfo))
		.Execute(fb->GetDevice(), fb->GetCommands()->GetTransferCommands());

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
	return (int)Mesh->StaticMesh->Collision->get_nodes().size() + MaxDynamicNodes;
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
	accelStructBLDesc.geometry.triangles.vertexData.deviceAddress = VertexBuffer->GetDeviceAddress() + vertexOffset * sizeof(SurfaceVertex);
	accelStructBLDesc.geometry.triangles.vertexStride = sizeof(SurfaceVertex);
	accelStructBLDesc.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	accelStructBLDesc.geometry.triangles.indexData.deviceAddress = IndexBuffer->GetDeviceAddress() + indexOffset * sizeof(uint32_t);
	accelStructBLDesc.geometry.triangles.maxVertex = submesh->MeshVertices.Size() - 1;

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
	instances[0].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	instances[0].accelerationStructureReference = StaticBLAS.AccelStruct->GetDeviceAddress();

	instances[1].transform.matrix[0][0] = 1.0f;
	instances[1].transform.matrix[1][1] = 1.0f;
	instances[1].transform.matrix[2][2] = 1.0f;
	instances[1].mask = 0xff;
	instances[1].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
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
	instances[0].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	instances[0].accelerationStructureReference = StaticBLAS.AccelStruct->GetDeviceAddress();

	instances[1].transform.matrix[0][0] = 1.0f;
	instances[1].transform.matrix[1][1] = 1.0f;
	instances[1].transform.matrix[2][2] = 1.0f;
	instances[1].mask = 0xff;
	instances[1].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
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

TArray<SurfaceInfo> VkRaytrace::CreateSurfaceInfo(LevelSubmesh* submesh)
{
	TArray<SurfaceInfo> surfaceInfo(submesh->GetSurfaceCount());
	for (int i = 0, count = submesh->GetSurfaceCount(); i < count; i++)
	{
		LevelMeshSurface* surface = submesh->GetSurface(i);

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

		surfaceInfo.Push(info);
	}
	return surfaceInfo;
}

TArray<CollisionNode> VkRaytrace::CreateCollisionNodes(LevelSubmesh* submesh)
{
	TArray<CollisionNode> nodes(submesh->Collision->get_nodes().size());
	for (const auto& node : submesh->Collision->get_nodes())
	{
		CollisionNode info;
		info.center = node.aabb.Center;
		info.extents = node.aabb.Extents;
		info.left = node.left;
		info.right = node.right;
		info.element_index = node.element_index;
		nodes.Push(info);
	}
	if (nodes.Size() == 0) // vulkan doesn't support zero byte buffers
		nodes.Push(CollisionNode());
	return nodes;
}

/////////////////////////////////////////////////////////////////////////////

BufferTransfer& BufferTransfer::AddBuffer(VulkanBuffer* buffer, size_t offset, const void* data, size_t size)
{
	bufferCopies.push_back({ buffer, offset, data, size, nullptr, 0 });
	return *this;
}

BufferTransfer& BufferTransfer::AddBuffer(VulkanBuffer* buffer, const void* data, size_t size)
{
	bufferCopies.push_back({ buffer, 0, data, size, nullptr, 0 });
	return *this;
}

BufferTransfer& BufferTransfer::AddBuffer(VulkanBuffer* buffer, const void* data0, size_t size0, const void* data1, size_t size1)
{
	bufferCopies.push_back({ buffer, 0, data0, size0, data1, size1 });
	return *this;
}

std::unique_ptr<VulkanBuffer> BufferTransfer::Execute(VulkanDevice* device, VulkanCommandBuffer* cmdbuffer)
{
	size_t transferbuffersize = 0;
	for (const auto& copy : bufferCopies)
		transferbuffersize += copy.size0 + copy.size1;

	if (transferbuffersize == 0)
		return nullptr;

	auto transferBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.Size(transferbuffersize)
		.DebugName("BufferTransfer.transferBuffer")
		.Create(device);

	uint8_t* data = (uint8_t*)transferBuffer->Map(0, transferbuffersize);
	size_t pos = 0;
	for (const auto& copy : bufferCopies)
	{
		memcpy(data + pos, copy.data0, copy.size0);
		pos += copy.size0;
		memcpy(data + pos, copy.data1, copy.size1);
		pos += copy.size1;
	}
	transferBuffer->Unmap();

	pos = 0;
	for (const auto& copy : bufferCopies)
	{
		if (copy.size0 > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), copy.buffer, pos, copy.offset, copy.size0);
		pos += copy.size0;

		if (copy.size1 > 0)
			cmdbuffer->copyBuffer(transferBuffer.get(), copy.buffer, pos, copy.offset + copy.size0, copy.size1);
		pos += copy.size1;
	}

	return transferBuffer;
}
