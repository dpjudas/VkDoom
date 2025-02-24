 
#include "vk_lightprober.h"
#include "vulkan/vk_renderdevice.h"
#include "vulkan/textures/vk_texture.h"
#include "vulkan/commands/vk_commandbuffer.h"
#include "vulkan/descriptorsets/vk_descriptorset.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include "vulkan/vk_renderstate.h"
#include "zvulkan/vulkanbuilders.h"
#include "filesystem.h"
#include "cmdlib.h"

VkLightprober::VkLightprober(VulkanRenderDevice* fb) : fb(fb)
{
	CreateIrradianceMap();
	CreatePrefilterMap();
	CreateEnvironmentMap();
}

VkLightprober::~VkLightprober()
{
}

void VkLightprober::CreateBrdfLutResources()
{
	brdfLut.shader = CompileShader("comp_brdf_convolute.glsl", "shaders/lightprobe/comp_brdf_convolute.glsl", "VkLightprober.BrdfLut");

	brdfLut.descriptorSetLayout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.Create(fb->GetDevice());

	brdfLut.pipelineLayout = PipelineLayoutBuilder()
		.AddSetLayout(brdfLut.descriptorSetLayout.get())
		.Create(fb->GetDevice());

	brdfLut.pipeline = ComputePipelineBuilder()
		.ComputeShader(brdfLut.shader.get())
		.Layout(brdfLut.pipelineLayout.get())
		.Create(fb->GetDevice());

	brdfLut.descriptorPool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1)
		.MaxSets(1)
		.Create(fb->GetDevice());

	brdfLut.descriptorSet = brdfLut.descriptorPool->allocate(brdfLut.descriptorSetLayout.get());

	brdfLut.image = ImageBuilder()
		.Size(512, 512)
		.Format(VK_FORMAT_R16G16_SFLOAT)
		.Usage(VK_IMAGE_USAGE_STORAGE_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
		.Create(fb->GetDevice());

	brdfLut.view = ImageViewBuilder()
		.Image(brdfLut.image.get(), VK_FORMAT_R16G16_SFLOAT)
		.Create(fb->GetDevice());

	WriteDescriptors()
		.AddStorageImage(brdfLut.descriptorSet.get(), 0, brdfLut.view.get(), VK_IMAGE_LAYOUT_GENERAL)
		.Execute(fb->GetDevice());
}

void VkLightprober::GenerateBrdfLut()
{
	auto staging = BufferBuilder()
		.Size(512 * 512 * 2 * sizeof(uint16_t))
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU)
		.Create(fb->GetDevice());

	auto cmdbuffer = fb->GetCommands()->GetTransferCommands();

	PipelineBarrier()
		.AddImage(brdfLut.image.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	cmdbuffer->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, brdfLut.pipeline.get());
	cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, brdfLut.pipelineLayout.get(), 0, brdfLut.descriptorSet.get());
	cmdbuffer->dispatch(512, 512, 1);

	PipelineBarrier()
		.AddImage(brdfLut.image.get(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkBufferImageCopy region = {};
	region.imageExtent.width = brdfLut.image->width;
	region.imageExtent.height = brdfLut.image->height;
	region.imageExtent.depth = 1;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	cmdbuffer->copyImageToBuffer(brdfLut.image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->buffer, 1, &region);

	fb->GetCommands()->WaitForCommands(false);

	std::vector<uint16_t> databuffer(512 * 512 * 2);
	auto src = staging->Map(0, databuffer.size() * sizeof(uint16_t));
	memcpy(databuffer.data(), src, databuffer.size());
	staging->Unmap();

	std::unique_ptr<FileWriter> file(FileWriter::Open("bdrf.lut"));
	file->Write(databuffer.data(), databuffer.size() * sizeof(uint16_t));
}

void VkLightprober::CreateEnvironmentMap()
{
	environmentMap.cubeimage = ImageBuilder()
		.Size(environmentMap.textureSize, environmentMap.textureSize, 1, 6)
		.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
		.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		.Flags(VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
		.DebugName("VkLightprober.environmentMap.cubeimage")
		.Create(fb->GetDevice());

	environmentMap.cubeview = ImageViewBuilder()
		.Type(VK_IMAGE_VIEW_TYPE_CUBE)
		.Image(environmentMap.cubeimage.get(), VK_FORMAT_R16G16B16A16_SFLOAT)
		.DebugName("VkLightprober.environmentMap.cubeview")
		.Create(fb->GetDevice());

	VkFormat format = fb->GetBuffers()->SceneDepthStencilFormat;

	environmentMap.zbuffer = ImageBuilder()
		.Size(environmentMap.textureSize, environmentMap.textureSize)
		.Samples(VK_SAMPLE_COUNT_1_BIT)
		.Format(format)
		.Usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		.DebugName("VkLightprober.environmentMap.zbuffer")
		.Create(fb->GetDevice());

	environmentMap.zbufferview = ImageViewBuilder()
		.Image(environmentMap.zbuffer.get(), format, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
		.DebugName("VkLightprober.environmentMap.zbufferview")
		.Create(fb->GetDevice());

	for (int i = 0; i < 6; i++)
	{
		environmentMap.renderTargets[i].View = ImageViewBuilder()
			.Image(environmentMap.cubeimage.get(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 0, i, 1, 1)
			.DebugName("VkLightprober.environmentMap.renderTargets[].View")
			.Create(fb->GetDevice());
	}
}

void VkLightprober::RenderEnvironmentMap(std::function<void(IntRect&, int)> renderFunc)
{
	auto renderstate = fb->GetRenderState();
	renderstate->EndRenderPass();

	PipelineBarrier()
		.AddImage(
			environmentMap.cubeimage.get(),
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0, 1, 0, 6)
		.Execute(
			fb->GetCommands()->GetDrawCommands(),
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	for (int side = 0; side < 6; side++)
	{
		environmentMap.renderTargets[side].Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		PipelineBarrier()
			.AddImage(
				environmentMap.zbuffer.get(),
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
			.Execute(
				fb->GetCommands()->GetDrawCommands(),
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);

		renderstate->SetRenderTarget(&environmentMap.renderTargets[side], environmentMap.zbufferview.get(), environmentMap.textureSize, environmentMap.textureSize, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT);

		IntRect bounds;
		bounds.left = 0;
		bounds.top = 0;
		bounds.width = environmentMap.textureSize;
		bounds.height = environmentMap.textureSize;
		renderFunc(bounds, side);

		renderstate->EndRenderPass();

		environmentMap.renderTargets[side].Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	PipelineBarrier()
		.AddImage(
			environmentMap.cubeimage.get(),
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0, 1, 0, 6)
		.Execute(
			fb->GetCommands()->GetDrawCommands(),
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	renderstate->SetRenderTarget(&fb->GetBuffers()->SceneColor, fb->GetBuffers()->SceneDepthStencil.View.get(), fb->GetBuffers()->GetWidth(), fb->GetBuffers()->GetHeight(), VK_FORMAT_R16G16B16A16_SFLOAT, fb->GetBuffers()->GetSceneSamples());
}

void VkLightprober::CreateIrradianceMap()
{
	irradianceMap.shader = CompileShader("comp_irradiance_convolute.glsl", "shaders/lightprobe/comp_irradiance_convolute.glsl", "VkLightprober.IrradianceMap");

	irradianceMap.descriptorSetLayout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.Create(fb->GetDevice());

	irradianceMap.descriptorSetLayout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.Create(fb->GetDevice());

	irradianceMap.pipelineLayout = PipelineLayoutBuilder()
		.AddSetLayout(irradianceMap.descriptorSetLayout.get())
		.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IrradianceMapPushConstants))
		.Create(fb->GetDevice());

	irradianceMap.pipeline = ComputePipelineBuilder()
		.ComputeShader(irradianceMap.shader.get())
		.Layout(irradianceMap.pipelineLayout.get())
		.Create(fb->GetDevice());

	irradianceMap.sampler = SamplerBuilder()
		.Create(fb->GetDevice());

	irradianceMap.descriptorPool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 6)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6)
		.MaxSets(6)
		.Create(fb->GetDevice());

	for (int i = 0; i < 6; i++)
	{
		irradianceMap.images[i] = ImageBuilder()
			.Size(32, 32)
			.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
			.Usage(VK_IMAGE_USAGE_STORAGE_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
			.Create(fb->GetDevice());

		irradianceMap.views[i] = ImageViewBuilder()
			.Image(irradianceMap.images[i].get(), VK_FORMAT_R16G16B16A16_SFLOAT)
			.Create(fb->GetDevice());

		irradianceMap.descriptorSets[i] = irradianceMap.descriptorPool->allocate(irradianceMap.descriptorSetLayout.get());
	}
}

bool VkLightprober::GenerateIrradianceMap(TArrayView<uint16_t>& databuffer)
{
	const int texelCount = DFrameBuffer::irrandiaceMapTexelCount;

	if (databuffer.Size() < texelCount)
	{
		return false;
	}

	auto staging = BufferBuilder()
		.Size(32 * 32 * 8 * 6)
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU)
		.Create(fb->GetDevice());

	WriteDescriptors write;
	for (int i = 0; i < 6; i++)
	{
		write.AddStorageImage(irradianceMap.descriptorSets[i].get(), 0, irradianceMap.views[i].get(), VK_IMAGE_LAYOUT_GENERAL);
		write.AddCombinedImageSampler(irradianceMap.descriptorSets[i].get(), 1, environmentMap.cubeview.get(), irradianceMap.sampler.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	write.Execute(fb->GetDevice());

	auto cmdbuffer = fb->GetCommands()->GetDrawCommands();

	PipelineBarrier barrier0;
	for (int i = 0; i < 6; i++)
		barrier0.AddImage(irradianceMap.images[i].get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
	barrier0.Execute(cmdbuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	cmdbuffer->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, irradianceMap.pipeline.get());

	// +x, -x, +y, -y, +z, -z

	FVector3 dir[6] = {
		FVector3( 1.0f,  0.0f,  0.0f),
		FVector3(-1.0f,  0.0f,  0.0f),
		FVector3( 0.0f, -1.0f,  0.0f),
		FVector3( 0.0f,  1.0f,  0.0f),
		FVector3( 0.0f,  0.0f,  1.0f),
		FVector3( 0.0f,  0.0f, -1.0f)
	};

	FVector3 up[6] = {
		FVector3(0.0f,  1.0f,  0.0f),
		FVector3(0.0f,  1.0f,  0.0f),
		FVector3(0.0f,  0.0f,  1.0f),
		FVector3(0.0f,  0.0f, -1.0f),
		FVector3(0.0f,  1.0f,  0.0f),
		FVector3(0.0f,  1.0f,  0.0f)
	};

	for (int i = 0; i < 6; i++)
	{
		FVector3 side = -(dir[i] ^ up[i]);

		IrradianceMapPushConstants push;
		push.dir = dir[i];
		push.side = side;
		push.up = up[i];

		cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, irradianceMap.pipelineLayout.get(), 0, irradianceMap.descriptorSets[i].get());
		cmdbuffer->pushConstants(irradianceMap.pipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IrradianceMapPushConstants), &push);
		cmdbuffer->dispatch(32, 32, 1);
	}

	PipelineBarrier barrier1;
	for (int i = 0; i < 6; i++)
		barrier1.AddImage(irradianceMap.images[i].get(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	barrier1.Execute(cmdbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	for (int i = 0; i < 6; i++)
	{
		VkBufferImageCopy region = { };
		region.bufferOffset = 32 * 32 * 8 * i;
		region.imageExtent.width = irradianceMap.images[i]->width;
		region.imageExtent.height = irradianceMap.images[i]->height;
		region.imageExtent.depth = 1;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		cmdbuffer->copyImageToBuffer(irradianceMap.images[i]->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->buffer, 1, &region);
	}

	fb->GetCommands()->WaitForCommands(false);

	// Copy while dropping the alpha channel
	auto dst = databuffer.Data();
	auto src = (uint16_t*)staging->Map(0, texelCount * 8);
	for (int i = 0; i < texelCount; i++)
	{
		*(dst++) = *(src++);
		*(dst++) = *(src++);
		*(dst++) = *(src++);
		src++;
	}
	staging->Unmap();
	return true;
}

void VkLightprober::CreatePrefilterMap()
{
	prefilterMap.shader = CompileShader("comp_prefilter_convolute.glsl", "shaders/lightprobe/comp_prefilter_convolute.glsl", "VkLightprober.PrefilterMap");

	prefilterMap.descriptorSetLayout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.Create(fb->GetDevice());

	prefilterMap.pipelineLayout = PipelineLayoutBuilder()
		.AddSetLayout(prefilterMap.descriptorSetLayout.get())
		.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PrefilterMapPushConstants))
		.Create(fb->GetDevice());

	prefilterMap.pipeline = ComputePipelineBuilder()
		.ComputeShader(prefilterMap.shader.get())
		.Layout(prefilterMap.pipelineLayout.get())
		.Create(fb->GetDevice());

	prefilterMap.sampler = SamplerBuilder()
		.Create(fb->GetDevice());

	prefilterMap.descriptorPool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 6 * prefilterMap.maxlevels)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6 * prefilterMap.maxlevels)
		.MaxSets(6 * prefilterMap.maxlevels)
		.Create(fb->GetDevice());

	for (int i = 0; i < 6; i++)
	{
		for (int level = 0; level < prefilterMap.maxlevels; level++)
		{
			int idx = i * prefilterMap.maxlevels + level;

			prefilterMap.images[idx] = ImageBuilder()
				.Size(128 >> level, 128 >> level)
				.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
				.Usage(VK_IMAGE_USAGE_STORAGE_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
				.Create(fb->GetDevice());

			prefilterMap.views[idx] = ImageViewBuilder()
				.Image(prefilterMap.images[idx].get(), VK_FORMAT_R16G16B16A16_SFLOAT)
				.Create(fb->GetDevice());

			prefilterMap.descriptorSets[idx] = prefilterMap.descriptorPool->allocate(prefilterMap.descriptorSetLayout.get());
		}
	}
}

bool VkLightprober::GeneratePrefilterMap(TArrayView<uint16_t>& databuffer)
{
	const int texelCount = DFrameBuffer::prefilterMapTexelCount;

	if (databuffer.Size() < texelCount)
	{
		return false;
	}

	auto staging = BufferBuilder()
		.Size(prefilterMap.levelsSize * 6 * 8)
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU)
		.Create(fb->GetDevice());

	WriteDescriptors write;
	for (int i = 0; i < 6 * prefilterMap.maxlevels; i++)
	{
		write.AddStorageImage(prefilterMap.descriptorSets[i].get(), 0, prefilterMap.views[i].get(), VK_IMAGE_LAYOUT_GENERAL);
		write.AddCombinedImageSampler(prefilterMap.descriptorSets[i].get(), 1, environmentMap.cubeview.get(), prefilterMap.sampler.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	write.Execute(fb->GetDevice());

	auto cmdbuffer = fb->GetCommands()->GetDrawCommands();

	PipelineBarrier barrier0;
	for (int i = 0; i < 6 * prefilterMap.maxlevels; i++)
		barrier0.AddImage(prefilterMap.images[i].get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
	barrier0.Execute(cmdbuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	cmdbuffer->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, prefilterMap.pipeline.get());

	// +x, -x, +y, -y, +z, -z

	FVector3 dir[6] = {
		FVector3( 1.0f,  0.0f,  0.0f),
		FVector3(-1.0f,  0.0f,  0.0f),
		FVector3( 0.0f, -1.0f,  0.0f),
		FVector3( 0.0f,  1.0f,  0.0f),
		FVector3( 0.0f,  0.0f,  1.0f),
		FVector3( 0.0f,  0.0f, -1.0f)
	};

	FVector3 up[6] = {
		FVector3(0.0f,  1.0f,  0.0f),
		FVector3(0.0f,  1.0f,  0.0f),
		FVector3(0.0f,  0.0f,  1.0f),
		FVector3(0.0f,  0.0f, -1.0f),
		FVector3(0.0f,  1.0f,  0.0f),
		FVector3(0.0f,  1.0f,  0.0f)
	};

	for (int i = 0; i < 6; i++)
	{
		FVector3 side = -(dir[i] ^ up[i]);

		PrefilterMapPushConstants push;
		push.dir = dir[i];
		push.side = side;
		push.up = up[i];

		for (int level = 0; level < prefilterMap.maxlevels; level++)
		{
			push.roughness = (float)level / (float)(prefilterMap.maxlevels - 1);

			cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, prefilterMap.pipelineLayout.get(), 0, prefilterMap.descriptorSets[i * prefilterMap.maxlevels + level].get());
			cmdbuffer->pushConstants(prefilterMap.pipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PrefilterMapPushConstants), &push);
			cmdbuffer->dispatch(128 >> level, 128 >> level, 1);
		}
	}

	PipelineBarrier barrier1;
	for (int i = 0; i < 6 * prefilterMap.maxlevels; i++)
		barrier1.AddImage(prefilterMap.images[i].get(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	barrier1.Execute(cmdbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	int offset = 0;
	for (int i = 0; i < 6 * prefilterMap.maxlevels; i++)
	{
		VkBufferImageCopy region = { };
		region.bufferOffset = offset;
		region.imageExtent.width = prefilterMap.images[i]->width;
		region.imageExtent.height = prefilterMap.images[i]->height;
		region.imageExtent.depth = 1;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		cmdbuffer->copyImageToBuffer(prefilterMap.images[i]->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->buffer, 1, &region);

		offset += prefilterMap.images[i]->width * prefilterMap.images[i]->height * 8;
	}

	fb->GetCommands()->WaitForCommands(false);

	// Copy while dropping the alpha channel
	auto dst = databuffer.Data();
	auto src = (uint16_t*)staging->Map(0, texelCount * 8);
	for (int i = 0; i < texelCount; i++)
	{
		*(dst++) = *(src++);
		*(dst++) = *(src++);
		*(dst++) = *(src++);
		src++;
	}
	staging->Unmap();
	return true;
}

std::unique_ptr<VulkanShader> VkLightprober::CompileShader(const std::string& name, const std::string& filename, const char* debugName)
{
	std::string prefix = "#version 460\r\n";
	prefix += "#extension GL_GOOGLE_include_directive : enable\n";
	prefix += "#extension GL_ARB_separate_shader_objects : enable\n";

	auto onIncludeLocal = [](std::string headerName, std::string includerName, size_t depth) { return OnInclude(headerName.c_str(), includerName.c_str(), depth, false); };
	auto onIncludeSystem = [](std::string headerName, std::string includerName, size_t depth) { return OnInclude(headerName.c_str(), includerName.c_str(), depth, true); };

	return ShaderBuilder()
		.Code(GLSLCompiler()
			.Type(ShaderType::Compute)
			.AddSource("VersionBlock", prefix)
			.AddSource(name, LoadPrivateShaderLump(filename.c_str()).GetChars())
			.OnIncludeLocal(onIncludeLocal)
			.OnIncludeSystem(onIncludeSystem)
			.Compile(fb->GetDevice()))
		.DebugName(debugName)
		.Create(debugName, fb->GetDevice());
}

FString VkLightprober::LoadPrivateShaderLump(const char* lumpname)
{
	int lump = fileSystem.CheckNumForFullName(lumpname, 0);
	if (lump == -1) I_Error("Unable to load '%s'", lumpname);
	return GetStringFromLump(lump);
}

FString VkLightprober::LoadPublicShaderLump(const char* lumpname)
{
	int lump = fileSystem.CheckNumForFullName(lumpname, 0);
	if (lump == -1) lump = fileSystem.CheckNumForFullName(lumpname);
	if (lump == -1) I_Error("Unable to load '%s'", lumpname);
	return GetStringFromLump(lump);
}

ShaderIncludeResult VkLightprober::OnInclude(FString headerName, FString includerName, size_t depth, bool system)
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
