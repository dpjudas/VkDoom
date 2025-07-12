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
#include "vulkan/shaders/vk_shadercache.h"
#include "hw_levelmesh.h"
#include "hw_material.h"
#include "texturemanager.h"
#include "cmdlib.h"

VkLevelMesh::VkLevelMesh(VulkanRenderDevice* fb) : fb(fb)
{
	useRayQuery = fb->IsRayQueryEnabled();
	if (useRayQuery)
		SphereBLAS = CreateSphereBLAS();
	CreateViewerObjects();
}

void VkLevelMesh::SetLevelMesh(LevelMesh* mesh)
{
	Mesh = mesh;
	ResetAccelStruct();
}

void VkLevelMesh::ResetAccelStruct()
{
	auto deletelist = fb->GetCommands()->DrawDeleteList.get();
	for (BLAS& blas : DynamicBLAS)
	{
		deletelist->Add(std::move(blas.ScratchBuffer));
		deletelist->Add(std::move(blas.AccelStructBuffer));
		deletelist->Add(std::move(blas.AccelStruct));
	}
	DynamicBLAS.clear();
	IndexesPerBLAS = 0;
	InstanceCount = 0;
	deletelist->Add(std::move(TopLevelAS.TransferBuffer));
	deletelist->Add(std::move(TopLevelAS.InstanceBuffer));
	deletelist->Add(std::move(TopLevelAS.ScratchBuffer));
	deletelist->Add(std::move(TopLevelAS.AccelStructBuffer));
	deletelist->Add(std::move(TopLevelAS.AccelStruct));
}

void VkLevelMesh::BeginFrame()
{
	bool accelStructNeedsUpdate = false;
	if (useRayQuery && IndexesPerBLAS != 0)
	{
		InstanceCount = (Mesh->Mesh.IndexCount + IndexesPerBLAS - 1) / IndexesPerBLAS;

		accelStructNeedsUpdate = Mesh->UploadRanges.Index.GetRanges().Size() != 0;
		for (const MeshBufferRange& range : Mesh->UploadRanges.Index.GetRanges())
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
	CheckAccelStruct();

	if (useRayQuery)
	{
		// Wait for uploads to finish
		PipelineBarrier()
			.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

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
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}
	else
	{
		// Uploads must finish before we can read from the shaders
		PipelineBarrier()
			.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
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
	CheckBuffers();
	VkLevelMeshUploader uploader(this);
	uploader.Upload();
}

void VkLevelMesh::CheckBuffers()
{
	auto deletelist = fb->GetCommands()->DrawDeleteList.get();
	bool buffersCreated = false;

	size_t size = std::max(Mesh->Mesh.Vertices.size(), (size_t)1) * sizeof(FFlatVertex);
	if (!VertexBuffer || size > VertexBuffer->size)
	{
		if (VertexBuffer)
			Mesh->UploadRanges.Vertex.Add(0, std::min(VertexBuffer->size / sizeof(FFlatVertex), Mesh->Mesh.Vertices.size()));

		buffersCreated = true;
		deletelist->Add(std::move(VertexBuffer));

		VertexBuffer = BufferBuilder()
			.Usage(
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
				VK_BUFFER_USAGE_TRANSFER_DST_BIT |
				(useRayQuery ?
					VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
					VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0) |
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
			.Size(size)
			.MinAlignment(16)
			.DebugName("VertexBuffer")
			.Create(fb->GetDevice());
	}

	size = std::max((size_t)Mesh->Mesh.Indexes.Size(), (size_t)1) * sizeof(uint32_t);
	if (!IndexBuffer || size > IndexBuffer->size)
	{
		if (IndexBuffer)
			Mesh->UploadRanges.Index.Add(0, std::min(IndexBuffer->size / sizeof(uint32_t), Mesh->Mesh.Indexes.size()));

		buffersCreated = true;
		deletelist->Add(std::move(IndexBuffer));

		IndexBuffer = BufferBuilder()
			.Usage(
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
				VK_BUFFER_USAGE_TRANSFER_DST_BIT |
				(useRayQuery ?
					VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
					VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0) |
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
			.Size(size)
			.MinAlignment(16)
			.DebugName("IndexBuffer")
			.Create(fb->GetDevice());
	}

	size = std::max(Mesh->Mesh.UniformIndexes.size(), (size_t)1) * sizeof(int);
	if (!UniformIndexBuffer || size > UniformIndexBuffer->size)
	{
		if (UniformIndexBuffer)
			Mesh->UploadRanges.UniformIndexes.Add(0, std::min(UniformIndexBuffer->size / sizeof(int), Mesh->Mesh.UniformIndexes.size()));

		buffersCreated = true;
		deletelist->Add(std::move(UniformIndexBuffer));

		UniformIndexBuffer = BufferBuilder()
			.Usage(
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
				VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.Size(size)
			.MinAlignment(16)
			.DebugName("UniformIndexes")
			.Create(fb->GetDevice());
	}

	size = sizeof(CollisionNodeBufferHeader) + std::max(Mesh->Mesh.Nodes.size(), (size_t)1) * sizeof(CollisionNode);
	if (!NodeBuffer || size > NodeBuffer->size)
	{
		if (NodeBuffer)
			Mesh->UploadRanges.Node.Add(0, std::min((NodeBuffer->size - sizeof(CollisionNodeBufferHeader)) / sizeof(CollisionNode), Mesh->Mesh.Nodes.size()));

		buffersCreated = true;
		deletelist->Add(std::move(NodeBuffer));

		NodeBuffer = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.Size(size)
			.MinAlignment(16)
			.DebugName("NodeBuffer")
			.Create(fb->GetDevice());
	}

	size = std::max(Mesh->Mesh.SurfaceIndexes.size(), (size_t)1) * sizeof(int);
	if (!SurfaceIndexBuffer || size > SurfaceIndexBuffer->size)
	{
		if (SurfaceIndexBuffer)
			Mesh->UploadRanges.SurfaceIndex.Add(0, std::min(SurfaceIndexBuffer->size / sizeof(int), Mesh->Mesh.SurfaceIndexes.size()));

		buffersCreated = true;
		deletelist->Add(std::move(SurfaceIndexBuffer));

		SurfaceIndexBuffer = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.Size(size)
			.MinAlignment(16)
			.DebugName("SurfaceBuffer")
			.Create(fb->GetDevice());
	}

	size = std::max(Mesh->Mesh.Surfaces.size(), (size_t)1) * sizeof(SurfaceInfo);
	if (!SurfaceBuffer || size > SurfaceBuffer->size)
	{
		if (SurfaceBuffer)
			Mesh->UploadRanges.Surface.Add(0, std::min(SurfaceBuffer->size / sizeof(SurfaceInfo), Mesh->Mesh.Surfaces.size()));

		buffersCreated = true;
		deletelist->Add(std::move(SurfaceBuffer));

		SurfaceBuffer = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.Size(size)
			.MinAlignment(16)
			.DebugName("SurfaceBuffer")
			.Create(fb->GetDevice());
	}

	size = std::max(Mesh->Mesh.Uniforms.size(), (size_t)1) * sizeof(SurfaceUniforms);
	if (!UniformsBuffer || size > UniformsBuffer->size)
	{
		if (UniformsBuffer)
			Mesh->UploadRanges.Uniforms.Add(0, std::min(UniformsBuffer->size / sizeof(SurfaceUniforms), Mesh->Mesh.Uniforms.size()));

		buffersCreated = true;
		deletelist->Add(std::move(UniformsBuffer));

		UniformsBuffer = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.Size(size)
			.MinAlignment(16)
			.DebugName("SurfaceUniformsBuffer")
			.Create(fb->GetDevice());
	}

	size = std::max(Mesh->Mesh.LightUniforms.size(), (size_t)1) * sizeof(SurfaceLightUniforms);
	if (!LightUniformsBuffer || size > LightUniformsBuffer->size)
	{
		if (LightUniformsBuffer)
			Mesh->UploadRanges.LightUniforms.Add(0, std::min(LightUniformsBuffer->size / sizeof(SurfaceLightUniforms), Mesh->Mesh.LightUniforms.size()));

		buffersCreated = true;
		deletelist->Add(std::move(LightUniformsBuffer));

		LightUniformsBuffer = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.Size(size)
			.MinAlignment(16)
			.DebugName("SurfaceLightUniformsBuffer")
			.Create(fb->GetDevice());
	}

	size = std::max(Mesh->Portals.size(), (size_t)1) * sizeof(PortalInfo);
	if (!PortalBuffer || size > PortalBuffer->size)
	{
		if (PortalBuffer)
			Mesh->UploadRanges.Portals.Add(0, std::min(PortalBuffer->size / sizeof(PortalInfo), Mesh->Portals.size()));

		buffersCreated = true;
		deletelist->Add(std::move(PortalBuffer));

		PortalBuffer = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.Size(size)
			.MinAlignment(16)
			.DebugName("PortalBuffer")
			.Create(fb->GetDevice());
	}

	size = std::max(Mesh->Mesh.Lights.size(), (size_t)1) * sizeof(LightInfo);
	if (!LightBuffer || size > LightBuffer->size)
	{
		if (LightBuffer)
			Mesh->UploadRanges.Light.Add(0, std::min(LightBuffer->size / sizeof(LightInfo), Mesh->Mesh.Lights.size()));

		buffersCreated = true;
		deletelist->Add(std::move(LightBuffer));

		LightBuffer = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.Size(size)
			.MinAlignment(16)
			.DebugName("LightBuffer")
			.Create(fb->GetDevice());
	}

	size = std::max(Mesh->Mesh.LightIndexes.size(), (size_t)1) * sizeof(int32_t);
	if (!LightIndexBuffer || size > LightIndexBuffer->size)
	{
		if (LightIndexBuffer)
			Mesh->UploadRanges.LightIndex.Add(0, std::min(LightIndexBuffer->size / sizeof(int32_t), Mesh->Mesh.LightIndexes.size()));

		buffersCreated = true;
		deletelist->Add(std::move(LightIndexBuffer));

		LightIndexBuffer = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.Size(size)
			.MinAlignment(16)
			.DebugName("LightIndexBuffer")
			.Create(fb->GetDevice());
	}

	size = std::max(Mesh->Mesh.DynLights.size(), (size_t)1);
	if (!DynLightBuffer || size > DynLightBuffer->size)
	{
		if (DynLightBuffer)
			Mesh->UploadRanges.DynLight.Add(0, std::min(DynLightBuffer->size, Mesh->Mesh.DynLights.size()));

		buffersCreated = true;
		deletelist->Add(std::move(DynLightBuffer));

		DynLightBuffer = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.Size(size)
			.MinAlignment(16)
			.DebugName("DynLightBuffer")
			.Create(fb->GetDevice());
	}

	if (buffersCreated)
		ResetAccelStruct();
}

void VkLevelMesh::CheckAccelStruct()
{
	if (useRayQuery && !TopLevelAS.AccelStruct)
	{
		// Wait for uploads to finish
		PipelineBarrier()
			.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		// Find out how many segments we should split the map into

		DynamicBLAS.resize(32);
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
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}
	else
	{
		// Uploads must finish before we can read from the shaders
		PipelineBarrier()
			.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}
}

VkLevelMesh::BLAS VkLevelMesh::CreateSphereBLAS()
{
	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	BLAS blas;

	VkAabbPositionsKHR positions[1] = {};
	positions[0].minX = -1.0;
	positions[0].minY = -1.0;
	positions[0].minZ = -1.0;
	positions[0].maxX = 1.0;
	positions[0].maxY = 1.0;
	positions[0].maxZ = 1.0;

	AabbPositionsBuffer = BufferBuilder()
		.Usage(
			VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)
		.Size(sizeof(VkAabbPositionsKHR))
		.MinAlignment(16)
		.DebugName("AabbPositionsBuffer")
		.Create(fb->GetDevice());

	auto transferBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.Size(sizeof(VkAabbPositionsKHR))
		.DebugName("AabbPositionsBuffer.Transfer")
		.Create(fb->GetDevice());
	void* dest = transferBuffer->Map(0, sizeof(VkAabbPositionsKHR));
	memcpy(dest, positions, sizeof(VkAabbPositionsKHR));
	transferBuffer->Unmap();

	cmdbuffer->copyBuffer(transferBuffer.get(), AabbPositionsBuffer.get());

	fb->GetCommands()->TransferDeleteList->Add(std::move(transferBuffer));

	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT)
		.Execute(fb->GetCommands()->GetTransferCommands(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	VkAccelerationStructureBuildGeometryInfoKHR buildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureGeometryKHR accelStructBLDesc = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	VkAccelerationStructureGeometryKHR* geometries[] = { &accelStructBLDesc };

	accelStructBLDesc.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
	accelStructBLDesc.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	accelStructBLDesc.geometry.aabbs = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR };
	accelStructBLDesc.geometry.aabbs.data.deviceAddress = AabbPositionsBuffer->GetDeviceAddress();
	accelStructBLDesc.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);

	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.ppGeometries = geometries;

	uint32_t maxPrimitiveCount = 1;

	VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	vkGetAccelerationStructureBuildSizesKHR(fb->GetDevice()->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &maxPrimitiveCount, &sizeInfo);

	blas.AccelStructBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
		.Size(sizeInfo.accelerationStructureSize)
		.MinAlignment(256)
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

	buildInfo.dstAccelerationStructure = blas.AccelStruct->accelstruct;
	buildInfo.scratchData.deviceAddress = blas.ScratchBuffer->GetDeviceAddress();

	VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
	VkAccelerationStructureBuildRangeInfoKHR* rangeInfos[] = { &rangeInfo };
	rangeInfo.primitiveCount = maxPrimitiveCount;

	cmdbuffer->buildAccelerationStructures(1, &buildInfo, rangeInfos);

	return blas;
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
	accelStructBLDesc.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
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
		.MinAlignment(256)
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
		.Size(sizeof(VkAccelerationStructureInstanceKHR) * (DynamicBLAS.size() + Mesh->Mesh.ActiveLights.size()))
		.DebugName("TopLevelAS.TransferBuffer")
		.Create(fb->GetDevice());

	TopLevelAS.InstanceBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(sizeof(VkAccelerationStructureInstanceKHR) * (DynamicBLAS.size() + Mesh->Mesh.ActiveLights.size()))
		.MinAlignment(16)
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

	uint32_t maxInstanceCount = (uint32_t)(DynamicBLAS.size() + Mesh->Mesh.ActiveLights.size());

	VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	vkGetAccelerationStructureBuildSizesKHR(fb->GetDevice()->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &maxInstanceCount, &sizeInfo);

	TopLevelAS.AccelStructBuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
		.Size(sizeInfo.accelerationStructureSize)
		.MinAlignment(256)
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
	rangeInfo.primitiveCount = (uint32_t)(instanceCount + Mesh->Mesh.ActiveLights.size());

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
	rangeInfo.primitiveCount = (uint32_t)(instanceCount + Mesh->Mesh.ActiveLights.size());

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

	auto data = (uint8_t*)TopLevelAS.TransferBuffer->Map(0, sizeof(VkAccelerationStructureInstanceKHR) * (DynamicBLAS.size() + Mesh->Mesh.ActiveLights.size()));
	for (int index = 0; index < InstanceCount; index++)
	{
		BLAS& blas = DynamicBLAS[index];
		instance.instanceCustomIndex = blas.InstanceCustomIndex;
		instance.accelerationStructureReference = blas.DeviceAddress;

		memcpy(data, &instance, sizeof(VkAccelerationStructureInstanceKHR));
		data += sizeof(VkAccelerationStructureInstanceKHR);
	}

	int lightIndex = 0;
	for (int activeLight : Mesh->Mesh.ActiveLights)
	{
		const auto& light = Mesh->Mesh.Lights[activeLight];
		instance.instanceCustomIndex = activeLight;
		instance.accelerationStructureReference = SphereBLAS.DeviceAddress;

		float radius = light.SoftShadowRadius * 2.0f;
		instance.transform.matrix[0][0] = radius;
		instance.transform.matrix[1][1] = radius;
		instance.transform.matrix[2][2] = radius;
		instance.transform.matrix[0][3] = light.Origin.X;
		instance.transform.matrix[1][3] = light.Origin.Z;
		instance.transform.matrix[2][3] = light.Origin.Y;

		memcpy(data, &instance, sizeof(VkAccelerationStructureInstanceKHR));
		data += sizeof(VkAccelerationStructureInstanceKHR);
		lightIndex++;
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
		builder.AddVertexShader(Viewer.VertexShader);
		builder.AddFragmentShader(Viewer.FragmentShader);
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
	pushconstants.CameraPos = cameraPos;
	pushconstants.ProjX = f / aspect;
	pushconstants.ProjY = f;
	pushconstants.SunDir = FVector3(Mesh->SunDirection.X, Mesh->SunDirection.Z, Mesh->SunDirection.Y);
	pushconstants.SunColor = Mesh->SunColor;
	pushconstants.SunIntensity = Mesh->SunIntensity;
	pushconstants.ViewX = (viewToWorld * FVector4(1.0f, 0.0f, 0.0f, 1.0f)).XYZ() - cameraPos;
	pushconstants.ViewY = (viewToWorld * FVector4(0.0f, 1.0f, 0.0f, 1.0f)).XYZ() - cameraPos;
	pushconstants.ViewZ = (viewToWorld * FVector4(0.0f, 0.0f, 1.0f, 1.0f)).XYZ() - cameraPos;
	pushconstants.ResolutionScaleX = 2.0f / fb->GetWidth();
	pushconstants.ResolutionScaleY = 2.0f / fb->GetHeight();

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

	Viewer.VertexShader = CachedGLSLCompiler()
		.Type(ShaderType::Vertex)
		.AddSource("versionblock", versionBlock)
		.AddSource("vert_viewer.glsl", LoadPrivateShaderLump("shaders/lightmap/vert_viewer.glsl").GetChars())
		.Compile(fb);

	Viewer.FragmentShader = CachedGLSLCompiler()
		.Type(ShaderType::Fragment)
		.AddSource("versionblock", versionBlock)
		.AddSource("frag_viewer.glsl", LoadPrivateShaderLump("shaders/lightmap/frag_viewer.glsl").GetChars())
		.Compile(fb);

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
	return fb->GetShaderCache()->GetPrivateFileText(lumpname);
}

FString VkLevelMesh::LoadPublicShaderLump(const char* lumpname)
{
	return fb->GetShaderCache()->GetPublicFileText(lumpname);
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

	// Resize node buffer if too small
	size_t neededNodeBufferSize = sizeof(CollisionNodeBufferHeader) + Mesh->Mesh->Mesh.Nodes.Size() * sizeof(CollisionNode);
	if (!Mesh->NodeBuffer || Mesh->NodeBuffer->size < neededNodeBufferSize)
	{
		auto oldBuffer = std::move(Mesh->NodeBuffer);

		Mesh->NodeBuffer = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
			.Size(neededNodeBufferSize * 2)
			.MinAlignment(16)
			.DebugName("NodeBuffer")
			.Create(Mesh->fb->GetDevice());

		if (oldBuffer)
		{
			VulkanCommandBuffer* cmdbuffer = Mesh->fb->GetCommands()->GetTransferCommands();
			cmdbuffer->copyBuffer(oldBuffer.get(), Mesh->NodeBuffer.get(), 0, 0, oldBuffer->size);

			// Make sure the buffer copy completes before updating the range changes
			PipelineBarrier()
				.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT)
				.Execute(cmdbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

			Mesh->fb->GetCommands()->TransferDeleteList->Add(std::move(oldBuffer));
		}
	}

	BeginTransfer(transferBufferSize);

	try
	{
		UploadNodes();
		UploadRanges(Mesh->Mesh->UploadRanges.Vertex, Mesh->Mesh->Mesh.Vertices.Data(), Mesh->VertexBuffer.get(), "VertexBuffer");
		UploadRanges(Mesh->Mesh->UploadRanges.UniformIndexes, Mesh->Mesh->Mesh.UniformIndexes.Data(), Mesh->UniformIndexBuffer.get(), "UniformIndexes");
		UploadRanges(Mesh->Mesh->UploadRanges.Index, Mesh->Mesh->Mesh.Indexes.Data(), Mesh->IndexBuffer.get(), "IndexBuffer");
		UploadRanges(Mesh->Mesh->UploadRanges.SurfaceIndex, Mesh->Mesh->Mesh.SurfaceIndexes.Data(), Mesh->SurfaceIndexBuffer.get(), "SurfaceIndexBuffer");
		UploadRanges(Mesh->Mesh->UploadRanges.LightIndex, Mesh->Mesh->Mesh.LightIndexes.Data(), Mesh->LightIndexBuffer.get(), "LightIndexBuffer");
		UploadRanges(Mesh->Mesh->UploadRanges.LightUniforms, Mesh->Mesh->Mesh.LightUniforms.Data(), Mesh->LightUniformsBuffer.get(), "LightUniformsBuffer");
		UploadRanges(Mesh->Mesh->UploadRanges.DynLight, Mesh->Mesh->Mesh.DynLights.Data(), Mesh->DynLightBuffer.get(), "DynLightBuffer");
		UploadSurfaces();
		UploadUniforms();
		UploadPortals();
		UploadLights();
		EndTransfer(transferBufferSize);
	}
	catch (...)
	{
		// vk_mem_alloc gets angry in an assert if we don't release the mapping of the buffer
		transferBuffer->Unmap();
		throw;
	}

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
	Mesh->Mesh->UploadRanges.Vertex.Clear();
	Mesh->Mesh->UploadRanges.Index.Clear();
	Mesh->Mesh->UploadRanges.Node.Clear();
	Mesh->Mesh->UploadRanges.SurfaceIndex.Clear();
	Mesh->Mesh->UploadRanges.Surface.Clear();
	Mesh->Mesh->UploadRanges.UniformIndexes.Clear();
	Mesh->Mesh->UploadRanges.Uniforms.Clear();
	Mesh->Mesh->UploadRanges.LightUniforms.Clear();
	Mesh->Mesh->UploadRanges.Portals.Clear();
	Mesh->Mesh->UploadRanges.Light.Clear();
	Mesh->Mesh->UploadRanges.LightIndex.Clear();
	Mesh->Mesh->UploadRanges.DynLight.Clear();
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
	datasize = transferBufferSize;
}

void VkLevelMeshUploader::EndTransfer(size_t transferBufferSize)
{
	transferBuffer->Unmap();

	if (datapos != transferBufferSize)
		I_FatalError("Transfer buffer size calculations did not match what was actually uploaded!");
}

static FVector3 SwapYZ(const FVector3& v)
{
	return FVector3(v.X, v.Z, v.Y);
}

void* VkLevelMeshUploader::GetUploadPtr(size_t size)
{
	if (datapos + size > datasize)
		I_FatalError("Out of bounds error in VkLevelMeshUploader (transfer buffer)!");
	return data + datapos;
}

void VkLevelMeshUploader::UploadData(VulkanBuffer* dest, size_t destOffset, const void* src, size_t size, const char* buffername)
{
	if (size == 0)
		return;

	if (datapos + size > datasize)
		I_FatalError("Out of bounds error uploading to %s (transfer buffer)!", buffername);

	if (destOffset + size > dest->size)
		I_FatalError("Out of bounds error uploading to %s (destination buffer)!", buffername);

	if (src)
		memcpy(data + datapos, src, size);

	copyCommands.emplace_back(transferBuffer.get(), dest, datapos, destOffset, size);
	datapos += size;
}

template<typename T>
void VkLevelMeshUploader::UploadRanges(const MeshBufferUploads& ranges, const T* srcbuffer, VulkanBuffer* destbuffer, const char* buffername)
{
	for (const MeshBufferRange& range : ranges.GetRanges())
	{
		UploadData(destbuffer, range.Start * sizeof(T), srcbuffer + range.Start, range.Count() * sizeof(T), buffername);
	}
}

void VkLevelMeshUploader::UploadNodes()
{
	if (Mesh->useRayQuery || Mesh->Mesh->UploadRanges.Node.GetRanges().Size() == 0)
		return;

	// Always update the header struct of the collision storage buffer block if something changed
	CollisionNodeBufferHeader nodesHeader;
	nodesHeader.root = Mesh->Mesh->Mesh.RootNode;
	UploadData(Mesh->NodeBuffer.get(), 0, &nodesHeader, sizeof(CollisionNodeBufferHeader), "NodeBuffer");

	// Copy collision nodes
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Node.GetRanges())
	{
		UploadData(Mesh->NodeBuffer.get(), sizeof(CollisionNodeBufferHeader) + range.Start * sizeof(CollisionNode), &Mesh->Mesh->Mesh.Nodes[range.Start], range.Count() * sizeof(CollisionNode), "NodeBuffer");
	}
}

void VkLevelMeshUploader::UploadSurfaces()
{
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Surface.GetRanges())
	{
		SurfaceInfo* surfaces = (SurfaceInfo*)GetUploadPtr(range.Count() * sizeof(SurfaceInfo));

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
				info.TextureIndex = Mesh->fb->GetBindlessTextureIndex(mat, CLAMP_NONE, 0, false);
			}
			else
			{
				info.TextureIndex = 0;
			}
			info.LightStart = surface->LightList.Pos;
			info.LightEnd = surface->LightList.Pos + surface->LightList.Count;

			*(surfaces++) = info;
		}

		UploadData(Mesh->SurfaceBuffer.get(), range.Start * sizeof(SurfaceInfo), nullptr, range.Count() * sizeof(SurfaceInfo), "SurfaceBuffer");
	}
}

void VkLevelMeshUploader::UploadUniforms()
{
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Uniforms.GetRanges())
	{
		for (int j = 0, count = range.Count(); j < count; j++)
		{
			auto& surfaceUniforms = Mesh->Mesh->Mesh.Uniforms[range.Start + j];
			auto& material = Mesh->Mesh->Mesh.Materials[range.Start + j];
			if (material.mMaterial)
			{
				auto source = material.mMaterial->Source();
				surfaceUniforms.uSpecularMaterial = { source->GetGlossiness(), source->GetSpecularLevel() };
				surfaceUniforms.uDepthFadeThreshold = source->GetDepthFadeThreshold();
				surfaceUniforms.uTextureIndex = Mesh->fb->GetBindlessTextureIndex(material.mMaterial, material.mClampMode, material.mTranslation, false);
			}
			else
			{
				surfaceUniforms.uDepthFadeThreshold = 0.f;
				surfaceUniforms.uTextureIndex = 0;
			}
		}

		UploadData(Mesh->UniformsBuffer.get(), range.Start * sizeof(SurfaceUniforms), &Mesh->Mesh->Mesh.Uniforms[range.Start], range.Count() * sizeof(SurfaceUniforms), "SurfaceUniforms");
	}
}

void VkLevelMeshUploader::UploadPortals()
{
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Portals.GetRanges())
	{
		PortalInfo* portals = (PortalInfo*)GetUploadPtr(range.Count() * sizeof(PortalInfo));
		for (int i = 0, count = range.Count(); i < count; i++)
		{
			const auto& portal = Mesh->Mesh->Portals[range.Start + i];
			PortalInfo info;
			info.transformation = portal.transformation;
			*(portals++) = info;
		}

		UploadData(Mesh->PortalBuffer.get(), range.Start * sizeof(PortalInfo), nullptr, range.Count() * sizeof(PortalInfo), "PortalBuffer");
	}
}

void VkLevelMeshUploader::UploadLights()
{
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Light.GetRanges())
	{
		LightInfo* lights = (LightInfo*)GetUploadPtr(range.Count() * sizeof(LightInfo));
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

		UploadData(Mesh->LightBuffer.get(), range.Start * sizeof(LightInfo), nullptr, range.Count() * sizeof(LightInfo), "LightBuffer");
	}
}

size_t VkLevelMeshUploader::GetTransferSize()
{
	// Figure out how much memory we need to transfer it to the GPU
	size_t transferBufferSize = 0;
	if (!Mesh->useRayQuery)
	{
		if (Mesh->Mesh->UploadRanges.Node.GetRanges().Size() > 0) transferBufferSize += sizeof(CollisionNodeBufferHeader);
		for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Node.GetRanges()) transferBufferSize += range.Count() * sizeof(CollisionNode);
	}
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Vertex.GetRanges()) transferBufferSize += range.Count() * sizeof(FFlatVertex);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.UniformIndexes.GetRanges()) transferBufferSize += range.Count() * sizeof(int);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Index.GetRanges()) transferBufferSize += range.Count() * sizeof(uint32_t);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.SurfaceIndex.GetRanges()) transferBufferSize += range.Count() * sizeof(int);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Surface.GetRanges()) transferBufferSize += range.Count() * sizeof(SurfaceInfo);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Uniforms.GetRanges()) transferBufferSize += range.Count() * sizeof(SurfaceUniforms);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.LightUniforms.GetRanges()) transferBufferSize += range.Count() * sizeof(SurfaceLightUniforms);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Portals.GetRanges()) transferBufferSize += range.Count() * sizeof(PortalInfo);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.LightIndex.GetRanges()) transferBufferSize += range.Count() * sizeof(int32_t);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.Light.GetRanges()) transferBufferSize += range.Count() * sizeof(LightInfo);
	for (const MeshBufferRange& range : Mesh->Mesh->UploadRanges.DynLight.GetRanges()) transferBufferSize += range.Count();
	return transferBufferSize;
}
