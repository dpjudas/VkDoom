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
	useRayQuery = fb->IsRayQueryEnabled();

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
	deletelist->Add(std::move(LightBuffer));
	deletelist->Add(std::move(LightIndexBuffer));
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
	UploadMeshes();

	if (useRayQuery)
	{
		// Wait for uploads to finish
		PipelineBarrier()
			.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		DynamicBLAS = CreateBLAS(true, 0, Mesh->Mesh.IndexCount);
		CreateTLASInstanceBuffer();

		UploadTLASInstanceBuffer();

		// Wait for bottom level builds to finish before using it as input to a toplevel accel structure. Also wait for the instance buffer upload to complete.
		PipelineBarrier()
			.AddMemory(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);

		CreateTopLevelAS(DynamicBLAS.AccelStruct ? 2 : 1);

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
	bool accelStructNeedsUpdate = Mesh->UploadRanges.Index.Size() != 0 || Mesh->UploadRanges.Vertex.Size() != 0;

	UploadMeshes();

	if (useRayQuery)
	{
		// Wait for uploads to finish
		PipelineBarrier()
			.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		if (accelStructNeedsUpdate)
		{
			// To do: we should reuse the buffers.

			auto deletelist = fb->GetCommands()->DrawDeleteList.get();
			deletelist->Add(std::move(DynamicBLAS.ScratchBuffer));
			deletelist->Add(std::move(DynamicBLAS.AccelStructBuffer));
			deletelist->Add(std::move(DynamicBLAS.AccelStruct));

			DynamicBLAS = CreateBLAS(true, 0, Mesh->Mesh.IndexCount);

			deletelist->Add(std::move(TopLevelAS.TransferBuffer));
			deletelist->Add(std::move(TopLevelAS.InstanceBuffer));
			CreateTLASInstanceBuffer();
			UploadTLASInstanceBuffer();
		}

		// Wait for bottom level builds to finish before using it as input to a toplevel accel structure. Also wait for the instance buffer upload to complete.
		PipelineBarrier()
			.AddMemory(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);

		UpdateTopLevelAS(DynamicBLAS.AccelStruct ? 2 : 1);

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

void VkLevelMesh::UploadMeshes()
{
	VkLevelMeshUploader uploader(this);
	uploader.Upload();
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
		.Size(Mesh->Mesh.Vertices.Size() * sizeof(FFlatVertex))
		.DebugName("VertexBuffer")
		.Create(fb->GetDevice());

	UniformIndexBuffer = BufferBuilder()
		.Usage(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(Mesh->Mesh.UniformIndexes.size() * sizeof(int))
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
		.Size((size_t)Mesh->Mesh.Indexes.Size() * sizeof(uint32_t))
		.DebugName("IndexBuffer")
		.Create(fb->GetDevice());

	NodeBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(sizeof(CollisionNodeBufferHeader) + Mesh->Mesh.MaxNodes * sizeof(CollisionNode))
		.DebugName("NodeBuffer")
		.Create(fb->GetDevice());

	SurfaceIndexBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(Mesh->Mesh.SurfaceIndexes.Size() * sizeof(int))
		.DebugName("SurfaceBuffer")
		.Create(fb->GetDevice());

	SurfaceBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(Mesh->Mesh.Surfaces.Size() * sizeof(SurfaceInfo))
		.DebugName("SurfaceBuffer")
		.Create(fb->GetDevice());

	UniformsBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(Mesh->Mesh.Uniforms.Size() * sizeof(SurfaceUniforms))
		.DebugName("SurfaceUniformsBuffer")
		.Create(fb->GetDevice());

	PortalBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(Mesh->Portals.Size() * sizeof(PortalInfo))
		.DebugName("PortalBuffer")
		.Create(fb->GetDevice());

	LightBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(Mesh->Mesh.Lights.Size() * sizeof(LightInfo))
		.DebugName("LightBuffer")
		.Create(fb->GetDevice());

	LightIndexBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(Mesh->Mesh.LightIndexes.Size() * sizeof(int32_t))
		.DebugName("LightIndexBuffer")
		.Create(fb->GetDevice());
}

VkLevelMesh::BLAS VkLevelMesh::CreateBLAS(bool preferFastBuild, int indexOffset, int indexCount)
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
	accelStructBLDesc.geometry.triangles.maxVertex = Mesh->Mesh.Vertices.Size() - 1;

	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildInfo.flags = preferFastBuild ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.ppGeometries = geometries;

	uint32_t maxPrimitiveCount = indexCount / 3;

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

void VkLevelMesh::CreateTopLevelAS(int instanceCount)
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
	rangeInfo.primitiveCount = instanceCount;

	fb->GetCommands()->GetTransferCommands()->buildAccelerationStructures(1, &buildInfo, rangeInfos);
}

void VkLevelMesh::UpdateTopLevelAS(int instanceCount)
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
	rangeInfo.primitiveCount = instanceCount;

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
	instances[0].accelerationStructureReference = DynamicBLAS.AccelStruct->GetDeviceAddress();

	/*
	instances[1].transform.matrix[0][0] = 1.0f;
	instances[1].transform.matrix[1][1] = 1.0f;
	instances[1].transform.matrix[2][2] = 1.0f;
	instances[1].mask = 0xff;
	instances[1].flags = 0;
	instances[1].accelerationStructureReference = DynamicBLAS.AccelStruct->GetDeviceAddress();
	*/

	auto data = (uint8_t*)TopLevelAS.TransferBuffer->Map(0, sizeof(VkAccelerationStructureInstanceKHR) * 2);
	memcpy(data, instances, sizeof(VkAccelerationStructureInstanceKHR) * 2);
	TopLevelAS.TransferBuffer->Unmap();

	fb->GetCommands()->GetTransferCommands()->copyBuffer(TopLevelAS.TransferBuffer.get(), TopLevelAS.InstanceBuffer.get());
}

/////////////////////////////////////////////////////////////////////////////

VkLevelMeshUploader::VkLevelMeshUploader(VkLevelMesh* mesh) : Mesh(mesh)
{
}

void VkLevelMeshUploader::Upload()
{
	size_t transferBufferSize = GetTransferSize();
	if (transferBufferSize == 0)
	{
		ClearRanges();
		return;
	}

	BeginTransfer(transferBufferSize);

	UploadNodes();
	UploadRanges(Mesh->Mesh->UploadRanges.Vertex, Mesh->Mesh->Mesh.Vertices.Data(), Mesh->VertexBuffer.get());
	UploadRanges(Mesh->Mesh->UploadRanges.UniformIndexes, Mesh->Mesh->Mesh.UniformIndexes.Data(), Mesh->UniformIndexBuffer.get());
	UploadRanges(Mesh->Mesh->UploadRanges.Index, Mesh->Mesh->Mesh.Indexes.Data(), Mesh->IndexBuffer.get());
	UploadRanges(Mesh->Mesh->UploadRanges.SurfaceIndex, Mesh->Mesh->Mesh.SurfaceIndexes.Data(), Mesh->SurfaceIndexBuffer.get());
	UploadRanges(Mesh->Mesh->UploadRanges.LightIndex, Mesh->Mesh->Mesh.LightIndexes.Data(), Mesh->LightIndexBuffer.get());
	UploadSurfaces();
	UploadUniforms();
	UploadPortals();
	UploadLights();

	EndTransfer(transferBufferSize);

	// We can't add these as we go because UploadUniforms might load textures, which may invalidate the transfer command buffer.
	VulkanCommandBuffer* cmdbuffer = Mesh->fb->GetCommands()->GetTransferCommands();
	for (const CopyCommand& copy : copyCommands)
	{
		cmdbuffer->copyBuffer(copy.srcBuffer, copy.dstBuffer, copy.srcOffset, copy.dstOffset, copy.size);
	}
	copyCommands.clear();
	Mesh->fb->GetCommands()->TransferDeleteList->Add(std::move(transferBuffer));

	ClearRanges();
}

void VkLevelMeshUploader::ClearRanges()
{
	Mesh->Mesh->UploadRanges.Vertex.clear();
	Mesh->Mesh->UploadRanges.Index.clear();
	Mesh->Mesh->UploadRanges.Node.clear();
	Mesh->Mesh->UploadRanges.SurfaceIndex.clear();
	Mesh->Mesh->UploadRanges.Surface.clear();
	Mesh->Mesh->UploadRanges.UniformIndexes.clear();
	Mesh->Mesh->UploadRanges.Uniforms.clear();
	Mesh->Mesh->UploadRanges.Portals.clear();
	Mesh->Mesh->UploadRanges.Light.clear();
	Mesh->Mesh->UploadRanges.LightIndex.clear();
}

void VkLevelMeshUploader::BeginTransfer(size_t transferBufferSize)
{
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
}

static FVector3 SwapYZ(const FVector3& v)
{
	return FVector3(v.X, v.Z, v.Y);
}

void VkLevelMeshUploader::UploadNodes()
{
	if (Mesh->useRayQuery)
		return;

	// Always update the header struct of the collision storage buffer block if something changed
	if (Mesh->Mesh->UploadRanges.Node.Size() > 0)
	{
		CollisionNodeBufferHeader nodesHeader;
		nodesHeader.root = Mesh->Mesh->Collision->get_root();

		*((CollisionNodeBufferHeader*)(data + datapos)) = nodesHeader;
		copyCommands.emplace_back(transferBuffer.get(), Mesh->NodeBuffer.get(), datapos, 0, sizeof(CollisionNodeBufferHeader));

		datapos += sizeof(CollisionNodeBufferHeader) + sizeof(CollisionNode);
	}

	// Copy collision nodes
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Node)
	{
		const auto& srcnodes = Mesh->Mesh->Collision->get_nodes();
		CollisionNode* nodes = (CollisionNode*)(data + datapos);
		for (int i = 0, count = range.Count(); i < count; i++)
		{
			const auto& node = srcnodes[range.Start + i];
			CollisionNode info;
			info.center = SwapYZ(node.aabb.Center);
			info.extents = SwapYZ(node.aabb.Extents);
			info.left = node.left;
			info.right = node.right;
			info.element_index = node.element_index;
			*(nodes++) = info;
		}

		size_t copysize = range.Count() * sizeof(CollisionNode);
		if (copysize > 0)
			copyCommands.emplace_back(transferBuffer.get(), Mesh->NodeBuffer.get(), datapos, sizeof(CollisionNodeBufferHeader) + range.Start * sizeof(CollisionNode), copysize);
		datapos += copysize;
	}
}

template<typename T>
void VkLevelMeshUploader::UploadRanges(const TArray<MeshBufferRange>& ranges, const T* srcbuffer, VulkanBuffer* destbuffer)
{
	for (const MeshBufferRange& range : ranges)
	{
		size_t copysize = range.Count() * sizeof(T);
		memcpy(data + datapos, srcbuffer + range.Start, copysize);
		if (copysize > 0)
			copyCommands.emplace_back(transferBuffer.get(), destbuffer, datapos, range.Start * sizeof(T), copysize);
		datapos += copysize;
	}
}

void VkLevelMeshUploader::UploadSurfaces()
{
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Surface)
	{
		SurfaceInfo* surfaces = (SurfaceInfo*)(data + datapos);
		for (int j = 0, count = range.Count(); j < count; j++)
		{
			LevelMeshSurface* surface = &Mesh->Mesh->Mesh.Surfaces[range.Start + j];

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
			info.LightStart = surface->LightList.Pos;
			info.LightEnd = surface->LightList.Pos + surface->LightList.Count;

			*(surfaces++) = info;
		}

		size_t copysize = range.Count() * sizeof(SurfaceInfo);
		if (copysize > 0)
			copyCommands.emplace_back(transferBuffer.get(), Mesh->SurfaceBuffer.get(), datapos, range.Start * sizeof(SurfaceInfo), copysize);
		datapos += copysize;
	}
}

void VkLevelMeshUploader::UploadUniforms()
{
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Uniforms)
	{
		for (int j = 0, count = range.Count(); j < count; j++)
		{
			auto& surfaceUniforms = Mesh->Mesh->Mesh.Uniforms[range.Start + j];
			auto& material = Mesh->Mesh->Mesh.Materials[range.Start + j];
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
		size_t copysize = range.Count() * sizeof(SurfaceUniforms);
		memcpy(uniforms, Mesh->Mesh->Mesh.Uniforms.Data() + range.Start, copysize);
		if (copysize > 0)
			copyCommands.emplace_back(transferBuffer.get(), Mesh->UniformsBuffer.get(), datapos, range.Start * sizeof(SurfaceUniforms), copysize);
		datapos += copysize;
	}
}

void VkLevelMeshUploader::UploadPortals()
{
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Portals)
	{
		PortalInfo* portals = (PortalInfo*)(data + datapos);
		for (int i = 0, count = range.Count(); i < count; i++)
		{
			const auto& portal = Mesh->Mesh->Portals[range.Start + i];
			PortalInfo info;
			info.transformation = portal.transformation;
			*(portals++) = info;
		}

		size_t copysize = range.Count() * sizeof(PortalInfo);
		if (copysize > 0)
			copyCommands.emplace_back(transferBuffer.get(), Mesh->PortalBuffer.get(), datapos, range.Start * sizeof(PortalInfo), copysize);
		datapos += copysize;
	}
}

void VkLevelMeshUploader::UploadLights()
{
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Light)
	{
		LightInfo* lights = (LightInfo*)(data + datapos);
		for (int i = 0, count = range.Count(); i < count; i++)
		{
			const auto& light = Mesh->Mesh->Mesh.Lights[range.Start + i];
			LightInfo info;
			info.Origin = SwapYZ(light.Origin);
			info.RelativeOrigin = SwapYZ(light.RelativeOrigin);
			info.Radius = light.Radius;
			info.Intensity = light.Intensity;
			info.InnerAngleCos = light.InnerAngleCos;
			info.OuterAngleCos = light.OuterAngleCos;
			info.SpotDir = SwapYZ(light.SpotDir);
			info.Color = light.Color;
			info.SoftShadowRadius = light.SoftShadowRadius;
			*(lights++) = info;
		}

		size_t copysize = range.Count() * sizeof(LightInfo);
		if (copysize > 0)
			copyCommands.emplace_back(transferBuffer.get(), Mesh->LightBuffer.get(), datapos, range.Start * sizeof(LightInfo), copysize);
		datapos += copysize;
	}
}

size_t VkLevelMeshUploader::GetTransferSize()
{
	// Figure out how much memory we need to transfer it to the GPU
	size_t transferBufferSize = 0;
	if (!Mesh->useRayQuery)
	{
		if (Mesh->Mesh->UploadRanges.Node.Size() > 0) transferBufferSize += sizeof(CollisionNodeBufferHeader) + sizeof(CollisionNode);
		for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Node) transferBufferSize += range.Count() * sizeof(CollisionNode);
	}
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Vertex) transferBufferSize += range.Count() * sizeof(FFlatVertex);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.UniformIndexes) transferBufferSize += range.Count() * sizeof(int);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Index) transferBufferSize += range.Count() * sizeof(uint32_t);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.SurfaceIndex) transferBufferSize += range.Count() * sizeof(int);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Surface) transferBufferSize += range.Count() * sizeof(SurfaceInfo);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Uniforms) transferBufferSize += range.Count() * sizeof(SurfaceUniforms);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Portals) transferBufferSize += range.Count() * sizeof(PortalInfo);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.LightIndex) transferBufferSize += range.Count() * sizeof(int32_t);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Light) transferBufferSize += range.Count() * sizeof(LightInfo);
	return transferBufferSize;
}
