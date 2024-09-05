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
#include "vulkan/descriptorsets/vk_descriptorset.h"
#include "vulkan/pipelines/vk_renderpass.h"
#include "hw_levelmesh.h"
#include "hw_material.h"
#include "texturemanager.h"
#include "cmdlib.h"

VkLevelMesh::VkLevelMesh(VulkanRenderDevice* fb) : fb(fb)
{
	useRayQuery = fb->IsRayQueryEnabled();
	CreateViewerObjects();

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
	deletelist->Add(std::move(DrawIndexBuffer));
	for (BLAS& blas : DynamicBLAS)
	{
		deletelist->Add(std::move(blas.ScratchBuffer));
		deletelist->Add(std::move(blas.AccelStructBuffer));
		deletelist->Add(std::move(blas.AccelStruct));
	}
	DynamicBLAS.clear();
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

		// Find out how many segments we should split the map into
		DynamicBLAS.resize(32); // fb->GetDevice()->PhysicalDevice.Properties.AccelerationStructure.maxInstanceCount is 65 or 382 on current devices (aug 2024)
		IndexesPerBLAS = ((Mesh->Mesh.Indexes.size() + 2) / 3 / DynamicBLAS.size() + 1) * 3;
		InstanceCount = (Mesh->Mesh.IndexCount + IndexesPerBLAS - 1) / IndexesPerBLAS;

		// Create a BLAS for each segment in use
		for (int instance = 0; instance < InstanceCount; instance++)
		{
			int indexStart = instance * IndexesPerBLAS;
			int indexEnd = std::min(indexStart + IndexesPerBLAS, Mesh->Mesh.IndexCount);
			DynamicBLAS[instance] = CreateBLAS(true, indexStart, indexEnd - indexStart);
		}

		CreateTLASInstanceBuffer();
		UploadTLASInstanceBuffer();

		// Wait for bottom level builds to finish before using it as input to a toplevel accel structure. Also wait for the instance buffer upload to complete.
		PipelineBarrier()
			.AddMemory(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);

		CreateTopLevelAS(InstanceCount);

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
	bool accelStructNeedsUpdate = false;
	if (useRayQuery)
	{
		InstanceCount = (Mesh->Mesh.IndexCount + IndexesPerBLAS - 1) / IndexesPerBLAS;

		accelStructNeedsUpdate = Mesh->UploadRanges.Index.Size() != 0;
		for (const MeshBufferRange& range : Mesh->UploadRanges.Index)
		{
			int start = range.Start / IndexesPerBLAS;
			int end = (range.End + IndexesPerBLAS - 1) / IndexesPerBLAS;
			for (int i = start; i < end; i++)
			{
				DynamicBLAS[i].NeedsUpdate = true;
			}
		}
	}

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

			// Create a new BLAS for each segment that changed
			auto deletelist = fb->GetCommands()->DrawDeleteList.get();
			for (int instance = 0; instance < InstanceCount; instance++)
			{
				BLAS& blas = DynamicBLAS[instance];
				if (blas.NeedsUpdate)
				{
					deletelist->Add(std::move(blas.ScratchBuffer));
					deletelist->Add(std::move(blas.AccelStructBuffer));
					deletelist->Add(std::move(blas.AccelStruct));

					int indexStart = instance * IndexesPerBLAS;
					int indexEnd = std::min(indexStart + IndexesPerBLAS, Mesh->Mesh.IndexCount);
					blas = CreateBLAS(true, indexStart, indexEnd - indexStart);
				}
			}

			deletelist->Add(std::move(TopLevelAS.TransferBuffer));
			deletelist->Add(std::move(TopLevelAS.InstanceBuffer));
			CreateTLASInstanceBuffer();
			UploadTLASInstanceBuffer();
		}

		// Wait for bottom level builds to finish before using it as input to a toplevel accel structure. Also wait for the instance buffer upload to complete.
		PipelineBarrier()
			.AddMemory(VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);

		UpdateTopLevelAS(InstanceCount);

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

	WriteDescriptors write;
	write.AddBuffer(Viewer.DescriptorSet.get(), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GetSurfaceIndexBuffer());
	write.AddBuffer(Viewer.DescriptorSet.get(), 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GetSurfaceBuffer());
	write.AddBuffer(Viewer.DescriptorSet.get(), 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GetLightBuffer());
	write.AddBuffer(Viewer.DescriptorSet.get(), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GetLightIndexBuffer());
	write.AddBuffer(Viewer.DescriptorSet.get(), 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GetPortalBuffer());
	if (useRayQuery)
	{
		write.AddAccelerationStructure(Viewer.DescriptorSet.get(), 5, GetAccelStruct());
	}
	else
	{
		write.AddBuffer(Viewer.DescriptorSet.get(), 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GetNodeBuffer());
	}
	write.AddBuffer(Viewer.DescriptorSet.get(), 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GetVertexBuffer());
	write.AddBuffer(Viewer.DescriptorSet.get(), 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GetIndexBuffer());
	write.Execute(fb->GetDevice());
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

	DrawIndexBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size((size_t)Mesh->Mesh.DrawIndexes.Size() * sizeof(uint32_t))
		.DebugName("DrawIndexBuffer")
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

	blas.DeviceAddress = blas.AccelStruct->GetDeviceAddress();
	blas.InstanceCustomIndex = indexOffset / 3;

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
		.Size(sizeof(VkAccelerationStructureInstanceKHR) * DynamicBLAS.size())
		.DebugName("TopLevelAS.TransferBuffer")
		.Create(fb->GetDevice());

	TopLevelAS.InstanceBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(sizeof(VkAccelerationStructureInstanceKHR) * DynamicBLAS.size())
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

	uint32_t maxInstanceCount = (uint32_t)DynamicBLAS.size();

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
	VkAccelerationStructureInstanceKHR instance = {};
	instance.transform.matrix[0][0] = 1.0f;
	instance.transform.matrix[1][1] = 1.0f;
	instance.transform.matrix[2][2] = 1.0f;
	instance.mask = 0xff;
	instance.flags = 0;

	auto data = (uint8_t*)TopLevelAS.TransferBuffer->Map(0, sizeof(VkAccelerationStructureInstanceKHR) * DynamicBLAS.size());
	for (BLAS& blas : DynamicBLAS)
	{
		instance.instanceCustomIndex = blas.InstanceCustomIndex;
		instance.accelerationStructureReference = blas.DeviceAddress;

		memcpy(data, &instance, sizeof(VkAccelerationStructureInstanceKHR));
		data += sizeof(VkAccelerationStructureInstanceKHR);
	}
	TopLevelAS.TransferBuffer->Unmap();

	fb->GetCommands()->GetTransferCommands()->copyBuffer(TopLevelAS.TransferBuffer.get(), TopLevelAS.InstanceBuffer.get());
}

void VkLevelMesh::RaytraceScene(const VkRenderPassKey& renderPassKey, VulkanCommandBuffer* commands, const FVector3& cameraPos, const VSMatrix& viewToWorld, float fovy, float aspect)
{
	auto& pipeline = Viewer.Pipeline[renderPassKey];
	if (!pipeline)
	{
		GraphicsPipelineBuilder builder;
		builder.RenderPass(fb->GetRenderPassManager()->GetRenderPass(renderPassKey)->GetRenderPass(0));
		builder.Layout(Viewer.PipelineLayout.get());
		builder.Topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		builder.AddVertexShader(Viewer.VertexShader.get());
		builder.AddFragmentShader(Viewer.FragmentShader.get());
		builder.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
		builder.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR);
		builder.RasterizationSamples((VkSampleCountFlagBits)renderPassKey.Samples);
		builder.DebugName("Viewer.Pipeline");
		pipeline = builder.Create(fb->GetDevice());
	}

	/*
	RenderPassBegin()
		.RenderPass(Viewer.RenderPass.get())
		.Framebuffer(Framebuffers[imageIndex].get())
		.RenderArea(0, 0, CurrentWidth, CurrentHeight)
		.AddClearColor(0.0f, 0.0f, 0.0f, 1.0f)
		.AddClearDepth(1.0f)
		.Execute(commands);

	VkViewport viewport = {};
	viewport.width = (float)CurrentWidth;
	viewport.height = (float)CurrentHeight;
	viewport.maxDepth = 1.0f;
	commands->setViewport(0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.extent.width = CurrentWidth;
	scissor.extent.height = CurrentHeight;
	commands->setScissor(0, 1, &scissor);
	*/

	float f = 1.0f / std::tan(fovy * (pi::pif() / 360.0f));

	ViewerPushConstants pushconstants;
	pushconstants.ViewToWorld = viewToWorld;
	pushconstants.CameraPos = cameraPos;
	pushconstants.ProjX = f / aspect;
	pushconstants.ProjY = f;
	pushconstants.SunDir = FVector3(Mesh->SunDirection.X, Mesh->SunDirection.Z, Mesh->SunDirection.Y);
	pushconstants.SunColor = Mesh->SunColor;
	pushconstants.SunIntensity = 1.0f;

	commands->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, Viewer.PipelineLayout.get(), 0, Viewer.DescriptorSet.get());
	commands->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, Viewer.PipelineLayout.get(), 1, fb->GetDescriptorSetManager()->GetBindlessSet());
	commands->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.get());
	commands->pushConstants(Viewer.PipelineLayout.get(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ViewerPushConstants), &pushconstants);
	commands->draw(4, 1, 0, 0);
	
	//commands->endRenderPass();
}

void VkLevelMesh::CreateViewerObjects()
{
	DescriptorSetLayoutBuilder builder;
	builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	if (useRayQuery)
	{
		builder.AddBinding(5, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	else
	{
		builder.AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	builder.AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.AddBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.DebugName("Viewer.DescriptorSetLayout");
	Viewer.DescriptorSetLayout = builder.Create(fb->GetDevice());

	DescriptorPoolBuilder poolbuilder;
	if (useRayQuery)
	{
		poolbuilder.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7);
		poolbuilder.AddPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1);
	}
	else
	{
		poolbuilder.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8);
	}
	poolbuilder.MaxSets(1);
	poolbuilder.DebugName("Viewer.DescriptorPool");
	Viewer.DescriptorPool = poolbuilder.Create(fb->GetDevice());

	Viewer.DescriptorSet = Viewer.DescriptorPool->allocate(Viewer.DescriptorSetLayout.get());
	Viewer.DescriptorSet->SetDebugName("raytrace.descriptorSet1");

	std::string versionBlock = R"(
			#version 460
			#extension GL_GOOGLE_include_directive : enable
			#extension GL_EXT_nonuniform_qualifier : enable
		)";

	if (useRayQuery)
	{
		versionBlock += "#extension GL_EXT_ray_query : require\r\n";
		versionBlock += "#define USE_RAYQUERY\r\n";
	}

	auto onIncludeLocal = [](std::string headerName, std::string includerName, size_t depth) { return OnInclude(headerName.c_str(), includerName.c_str(), depth, false); };
	auto onIncludeSystem = [](std::string headerName, std::string includerName, size_t depth) { return OnInclude(headerName.c_str(), includerName.c_str(), depth, true); };

	Viewer.VertexShader = ShaderBuilder()
		.Type(ShaderType::Vertex)
		.AddSource("versionblock", versionBlock)
		.AddSource("vert_viewer.glsl", LoadPrivateShaderLump("shaders/lightmap/vert_viewer.glsl").GetChars())
		.OnIncludeLocal(onIncludeLocal)
		.OnIncludeSystem(onIncludeSystem)
		.DebugName("Viewer.VertexShader")
		.Create("vertex", fb->GetDevice());

	Viewer.FragmentShader = ShaderBuilder()
		.Type(ShaderType::Fragment)
		.AddSource("versionblock", versionBlock)
		.AddSource("frag_viewer.glsl", LoadPrivateShaderLump("shaders/lightmap/frag_viewer.glsl").GetChars())
		.OnIncludeLocal(onIncludeLocal)
		.OnIncludeSystem(onIncludeSystem)
		.DebugName("Viewer.FragmentShader")
		.Create("vertex", fb->GetDevice());

	Viewer.PipelineLayout = PipelineLayoutBuilder()
		.AddSetLayout(Viewer.DescriptorSetLayout.get())
		.AddSetLayout(fb->GetDescriptorSetManager()->GetBindlessLayout())
		.AddPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ViewerPushConstants))
		.DebugName("Viewer.PipelineLayout")
		.Create(fb->GetDevice());

	/*
	Viewer.RenderPass = RenderPassBuilder()
		.AddAttachment(VK_FORMAT_B8G8R8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
		.AddSubpass()
		.AddSubpassColorAttachmentRef(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		.Create(fb->GetDevice());

	Viewer.Pipeline = GraphicsPipelineBuilder()
		.RenderPass(Viewer.RenderPass.get())
		.Layout(Viewer.PipelineLayout.get())
		.Topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
		.AddVertexShader(Viewer.VertexShader.get())
		.AddFragmentShader(Viewer.FragmentShader.get())
		.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
		.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
		.DebugName("Viewer.Pipeline")
		.Create(fb->GetDevice());
	*/
}

FString VkLevelMesh::LoadPrivateShaderLump(const char* lumpname)
{
	int lump = fileSystem.CheckNumForFullName(lumpname, 0);
	if (lump == -1) I_Error("Unable to load '%s'", lumpname);
	return GetStringFromLump(lump);
}

FString VkLevelMesh::LoadPublicShaderLump(const char* lumpname)
{
	int lump = fileSystem.CheckNumForFullName(lumpname, 0);
	if (lump == -1) lump = fileSystem.CheckNumForFullName(lumpname);
	if (lump == -1) I_Error("Unable to load '%s'", lumpname);
	return GetStringFromLump(lump);
}

ShaderIncludeResult VkLevelMesh::OnInclude(FString headerName, FString includerName, size_t depth, bool system)
{
	if (depth > 8)
		I_Error("Too much include recursion!");

	FString includeguardname;
	includeguardname << "_HEADERGUARD_" << headerName.GetChars();
	includeguardname.ReplaceChars("/\\.", '_');

	FString code;
	code << "#ifndef " << includeguardname.GetChars() << "\n";
	code << "#define " << includeguardname.GetChars() << "\n";
	code << "#line 1\n";

	if (system)
		code << LoadPrivateShaderLump(headerName.GetChars()).GetChars() << "\n";
	else
		code << LoadPublicShaderLump(headerName.GetChars()).GetChars() << "\n";

	code << "#endif\n";

	return ShaderIncludeResult(headerName.GetChars(), code.GetChars());
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
	UploadRanges(Mesh->Mesh->UploadRanges.DrawIndex, Mesh->Mesh->Mesh.DrawIndexes.Data(), Mesh->DrawIndexBuffer.get());
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
	Mesh->Mesh->UploadRanges.DrawIndex.clear();
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
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.DrawIndex) transferBufferSize += range.Count() * sizeof(uint32_t);
	return transferBufferSize;
}
