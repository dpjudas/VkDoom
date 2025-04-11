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

#include "vk_renderpass.h"
#include "vk_pprenderpass.h"
#include "vulkan/vk_renderstate.h"
#include "vulkan/vk_renderdevice.h"
#include "vulkan/vk_levelmesh.h"
#include "vulkan/descriptorsets/vk_descriptorset.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include "vulkan/samplers/vk_samplers.h"
#include "vulkan/shaders/vk_shader.h"
#include "vulkan/buffers/vk_hwbuffer.h"
#include <zvulkan/vulkanbuilders.h>
#include "flatvertices.h"
#include "hw_viewpointuniforms.h"
#include "v_2ddrawer.h"
#include "i_specialpaths.h"
#include "cmdlib.h"

CVAR(Bool, gl_ubershaders, true, 0);
CVAR(Bool, vk_debug_pipeline_creation, false, 0);
EXTERN_CVAR(Bool, gl_multithread);
CUSTOM_CVAR(Int, vk_pipeline_threads, 0, 0) // Todo: archive?
{
	if (self < 0) self = 0;
}

// Pipeline creation tracking and printing
int Printf(const char* fmt, ...);
static double pipeline_time;
static int pipeline_count;
ADD_STAT(pipelines)
{
	FString out;
	out.Format(
		"Pipelines created: %d\n"
		"Pipeline time: %.3f\n"
		"Pipeline average: %.3f",
		pipeline_count, pipeline_time, pipeline_time / pipeline_count);
	return out;
}

static unsigned MaxPipelineThreads()
{
	unsigned usedThreads = 2; // this thread + one extra
	unsigned hwLimit = std::thread::hardware_concurrency();

	if (gl_multithread)
		usedThreads++;

	return (usedThreads >= hwLimit) ? 1u : hwLimit - usedThreads; // max(1, hwLimit - usedThreads)
}

static unsigned CalculatePipelineThreadCountTarget()
{
	unsigned requested = static_cast<unsigned>(vk_pipeline_threads);

	if (requested <= 0)
	{
		requested = std::thread::hardware_concurrency() / 2;
	}

	return clamp(requested, 1u, MaxPipelineThreads());
}

VkRenderPassManager::VkRenderPassManager(VulkanRenderDevice* fb) : fb(fb)
{
	FString path = M_GetCachePath(true);
	CreatePath(path.GetChars());
	CacheFilename = path + "/pipelinecache.zdpc";

	PipelineCacheBuilder builder;
	builder.DebugName("PipelineCache");

	try
	{
		FileReader fr;
		if (fr.OpenFile(CacheFilename.GetChars()))
		{
			std::vector<uint8_t> data;
			data.resize(fr.GetLength());
			if (fr.Read(data.data(), data.size()) == (FileReader::Size)data.size())
			{
				builder.InitialData(data.data(), data.size());
			}
		}
	}
	catch (...)
	{
	}

	PipelineCache = builder.Create(fb->GetDevice());

	CreatePipelineWorkThreads();
	CreateLightTilesPipeline();
	CreateZMinMaxPipeline();
}

VkRenderPassManager::~VkRenderPassManager()
{
	StopWorkerThreads();

	try
	{
		auto data = PipelineCache->GetCacheData();
		std::unique_ptr<FileWriter> fw(FileWriter::Open(CacheFilename.GetChars()));
		if (fw)
			fw->Write(data.data(), data.size());
	}
	catch (...)
	{
	}
}

void VkRenderPassManager::ProcessMainThreadTasks()
{
	if (CalculatePipelineThreadCountTarget() != Worker.Threads.size())
	{
		StopWorkerThreads();
		CreatePipelineWorkThreads();
	}

	std::unique_lock lock(Worker.Mutex);
	std::vector<std::function<void()>> tasks;
	tasks.swap(Worker.MainTasks);
	lock.unlock();

	for (auto& task : tasks)
	{
		task();
	}
}

void VkRenderPassManager::RunOnWorkerThread(std::function<void()> task, bool precache)
{
	std::unique_lock lock(Worker.Mutex);
	if (precache)
		Worker.PrecacheTasks.push_back(std::move(task));
	else
		Worker.PriorityTasks.push_back(std::move(task));
	lock.unlock();
	Worker.CondVar.notify_one();
}

void VkRenderPassManager::RunOnMainThread(std::function<void()> task)
{
	std::unique_lock lock(Worker.Mutex);
	Worker.MainTasks.push_back(std::move(task));
}

void VkRenderPassManager::StopWorkerThreads()
{
	std::unique_lock lock(Worker.Mutex);
	Worker.StopFlag = true;
	lock.unlock();
	Worker.CondVar.notify_all();
	for (auto& thread : Worker.Threads)
	{
		thread->join();
	}
	lock.lock();
	Worker.Threads.clear();
	Worker.PrecacheTasks.clear();
	Worker.PriorityTasks.clear();
	Worker.StopFlag = false;
}

void VkRenderPassManager::WorkerThreadMain()
{
	std::unique_lock lock(Worker.Mutex);
	while (true)
	{
		Worker.CondVar.wait(lock, [&] { return Worker.StopFlag || !Worker.PrecacheTasks.empty() || !Worker.PriorityTasks.empty(); });
		if (Worker.StopFlag)
			break;

		std::function<void()> task;

		if (!Worker.PriorityTasks.empty())
		{
			task = std::move(Worker.PriorityTasks.front());
			Worker.PriorityTasks.pop_front();
		}
		else if (!Worker.PrecacheTasks.empty())
		{
			task = std::move(Worker.PrecacheTasks.front());
			Worker.PrecacheTasks.pop_front();
		}

		if (task)
		{
			lock.unlock();

			try
			{
				task();
			}
			catch (...)
			{
				auto exception = std::current_exception();
				RunOnMainThread([=]() { std::rethrow_exception(exception); });
			}

			lock.lock();
		}
	}
}

void VkRenderPassManager::RenderBuffersReset()
{
	RenderPassSetup.clear();
	PPRenderPassSetup.clear();
}

VkRenderPassSetup *VkRenderPassManager::GetRenderPass(const VkRenderPassKey &key)
{
	auto &item = RenderPassSetup[key];
	if (!item)
		item.reset(new VkRenderPassSetup(fb, key));
	return item.get();
}

int VkRenderPassManager::GetVertexFormat(const std::vector<size_t>& bufferStrides, const std::vector<FVertexBufferAttribute>& attrs)
{
	for (size_t i = 0; i < VertexFormats.size(); i++)
	{
		const auto &f = VertexFormats[i];
		if (f.BufferStrides.size() == bufferStrides.size() &&
			f.Attrs.size() == attrs.size() &&
			memcmp(f.BufferStrides.data(), bufferStrides.data(), bufferStrides.size() * sizeof(size_t)) == 0 &&
			memcmp(f.Attrs.data(), attrs.data(), attrs.size() * sizeof(FVertexBufferAttribute)) == 0)
		{
			return (int)i;
		}
	}

	VkVertexFormat fmt;
	fmt.BufferStrides = bufferStrides;
	fmt.Attrs = attrs;
	fmt.UseVertexData = 0;
	for (const FVertexBufferAttribute& attr : fmt.Attrs)
	{
		if (attr.location == VATTR_COLOR)
			fmt.UseVertexData |= 1;
		else if (attr.location == VATTR_NORMAL)
			fmt.UseVertexData |= 2;
	}
	VertexFormats.push_back(fmt);
	return (int)VertexFormats.size() - 1;
}

VkVertexFormat *VkRenderPassManager::GetVertexFormat(int index)
{
	return &VertexFormats[index];
}

VulkanPipelineLayout* VkRenderPassManager::GetPipelineLayout(bool levelmesh, int UserUniformSize)
{
	auto &layout = PipelineLayouts[levelmesh][UserUniformSize];
	if (layout)
		return layout.get();

	auto descriptors = fb->GetDescriptorSetManager();

	PipelineLayoutBuilder builder;
	builder.AddSetLayout(descriptors->GetFixedLayout());
	builder.AddSetLayout(levelmesh ? descriptors->GetLevelMeshLayout() : descriptors->GetRSBufferLayout());
	builder.AddSetLayout(descriptors->GetBindlessLayout());
	builder.AddPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants) + UserUniformSize);

	builder.DebugName("VkRenderPassManager.PipelineLayout");
	layout = builder.Create(fb->GetDevice());
	return layout.get();
}

VkPPRenderPassSetup* VkRenderPassManager::GetPPRenderPass(const VkPPRenderPassKey& key)
{
	auto& passSetup = PPRenderPassSetup[key];
	if (!passSetup)
		passSetup.reset(new VkPPRenderPassSetup(fb, key));
	return passSetup.get();
}

void VkRenderPassManager::CreatePipelineWorkThreads()
{
	unsigned threadCount = CalculatePipelineThreadCountTarget();

	Worker.Threads.reserve(threadCount);

	for (unsigned i = 0; i < threadCount; ++i)
	{
		Worker.Threads.emplace_back(new std::thread([this]() { WorkerThreadMain(); }));
	}

	if (vk_debug_pipeline_creation)
	{
		Printf("VkRenderPassManager: Created %d worker threads\n", threadCount);
	}
}

void VkRenderPassManager::CreateLightTilesPipeline()
{
	LightTiles.Layout = PipelineLayoutBuilder()
		.AddSetLayout(fb->GetDescriptorSetManager()->GetLightTilesLayout())
		.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(LightTilesPushConstants))
		.DebugName("VkRenderPassManager.LightTiles.Layout")
		.Create(fb->GetDevice());

	LightTiles.Pipeline = ComputePipelineBuilder()
		.Cache(PipelineCache.get())
		.Layout(LightTiles.Layout.get())
		.ComputeShader(fb->GetShaderManager()->GetLightTilesShader())
		.DebugName("VkRenderPassManager.LightTiles.Pipeline")
		.Create(fb->GetDevice());
}

void VkRenderPassManager::CreateZMinMaxPipeline()
{
	ZMinMax.RenderPass = RenderPassBuilder()
		.AddAttachment(VK_FORMAT_R32G32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		.AddExternalSubpassDependency(
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
		.AddSubpass()
		.AddSubpassColorAttachmentRef(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		.DebugName("VkRenderPassManager.ZMinMax.RenderPass")
		.Create(fb->GetDevice());

	ZMinMax.Layout = PipelineLayoutBuilder()
		.AddSetLayout(fb->GetDescriptorSetManager()->GetZMinMaxLayout())
		.AddPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ZMinMaxPushConstants))
		.DebugName("VkRenderPassManager.ZMinMax.Layout")
		.Create(fb->GetDevice());

	for (int i = 0; i < 3; i++)
	{
		ZMinMax.Pipeline[i] = GraphicsPipelineBuilder()
			.Cache(PipelineCache.get())
			.RenderPass(ZMinMax.RenderPass.get())
			.Layout(ZMinMax.Layout.get())
			.AddVertexShader(fb->GetShaderManager()->GetZMinMaxVertexShader())
			.AddFragmentShader(fb->GetShaderManager()->GetZMinMaxFragmentShader(i))
			.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
			.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
			.DebugName("VkRenderPassManager.ZMinMax.Pipeline")
			.Create(fb->GetDevice());
	}
}

/////////////////////////////////////////////////////////////////////////////

VkRenderPassSetup::VkRenderPassSetup(VulkanRenderDevice* fb, const VkRenderPassKey &key) : PassKey(key), fb(fb)
{
	// Precompile material fragment shaders:
	if (gl_ubershaders)
	{
		VkPipelineKey fkey;
		fkey.IsGeneralized = true;
		fkey.DepthFunc = DF_LEqual;
		fkey.StencilTest = true;
		fkey.StencilPassOp = SOP_Keep;
		fkey.ShaderKey.Layout.Simple = 0;
		fkey.ShaderKey.Layout.Simple3D = 0;
		fkey.ShaderKey.Layout.GBufferPass = gl_ssao != 0;
		fkey.ShaderKey.Layout.UseLevelMesh = 0;

		// Ignore these. They all look horrible. We should delete them.
		bool skip[NUM_BUILTIN_SHADERS] = {};
		skip[SHADER_BasicFuzz] = true;
		skip[SHADER_SmoothFuzz] = true;
		skip[SHADER_SwirlyFuzz] = true;
		skip[SHADER_TranslucentFuzz] = true;
		skip[SHADER_JaggedFuzz] = true;
		skip[SHADER_NoiseFuzz] = true;
		skip[SHADER_SmoothNoiseFuzz] = true;

		int count = NUM_BUILTIN_SHADERS + usershaders.Size();
		for (int i = 0; i < count; i++)
		{
			fkey.ShaderKey.SpecialEffect = EFF_NONE;
			fkey.ShaderKey.EffectState = i;
			for (int j = 0; j < 16; j++)
			{
				fkey.DepthWrite = (j & 1) != 0;
				fkey.DepthTest = (j & 2) != 0;
				fkey.ShaderKey.Layout.AlphaTest = (j & 4) != 0;
				fkey.ShaderKey.Layout.ShadeVertex = (j & 8) != 0;
				PrecompileFragmentShaderLibrary(fkey, true);
			}
		}
	}
}

std::unique_ptr<VulkanRenderPass> VkRenderPassSetup::CreateRenderPass(int clearTargets)
{
	auto buffers = fb->GetBuffers();

	VkFormat drawBufferFormats[] = { VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, buffers->SceneNormalFormat };

	RenderPassBuilder builder;

	builder.AddAttachment(
		PassKey.DrawBufferFormat, (VkSampleCountFlagBits)PassKey.Samples,
		(clearTargets & CT_Color) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	for (int i = 1; i < PassKey.DrawBuffers; i++)
	{
		builder.AddAttachment(
			drawBufferFormats[i], (VkSampleCountFlagBits)PassKey.Samples,
			(clearTargets & CT_Color) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}
	if (PassKey.DepthStencil)
	{
		builder.AddDepthStencilAttachment(
			buffers->SceneDepthStencilFormat, (VkSampleCountFlagBits)PassKey.Samples,
			(clearTargets & CT_Depth) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			(clearTargets & CT_Stencil) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}
	builder.AddSubpass();
	for (int i = 0; i < PassKey.DrawBuffers; i++)
		builder.AddSubpassColorAttachmentRef(i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	if (PassKey.DepthStencil)
	{
		builder.AddSubpassDepthStencilAttachmentRef(PassKey.DrawBuffers, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		builder.AddExternalSubpassDependency(
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
	}
	else
	{
		builder.AddExternalSubpassDependency(
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
	}
	builder.DebugName("VkRenderPassSetup.RenderPass");
	return builder.Create(fb->GetDevice());
}

VulkanRenderPass *VkRenderPassSetup::GetRenderPass(int clearTargets)
{
	if (!RenderPasses[clearTargets])
		RenderPasses[clearTargets] = CreateRenderPass(clearTargets);
	return RenderPasses[clearTargets].get();
}

VulkanPipeline *VkRenderPassSetup::GetPipeline(const VkPipelineKey &key, UniformStructHolder &Uniforms)
{
	if (gl_ubershaders)
	{
		auto data = GetSpecializedPipeline(key);
		if (!data)
			data = GetGeneralizedPipeline(key);
		Uniforms = data->Uniforms;
		return data->pipeline.get();
	}
	else
	{
		auto item = SpecializedPipelines.find(key);
		if (item == SpecializedPipelines.end() || item->second.pipeline == nullptr)
		{
			auto pipeline = CreateWithStats(*CreatePipeline(key, false, Uniforms), "Specialized");
			auto ptr = pipeline.get();
			SpecializedPipelines.insert(std::pair<VkPipelineKey, PipelineData>{key, PipelineData{std::move(pipeline), Uniforms}});
			return ptr;
		}
		else
		{
			Uniforms = item->second.Uniforms;
			return item->second.pipeline.get();
		}
	}
}

PipelineData* VkRenderPassSetup::GetSpecializedPipeline(const VkPipelineKey& key)
{
	// Have we seen this before?
	auto it = SpecializedPipelines.find(key);
	if (it != SpecializedPipelines.end())
	{
		// Yes. Do we have the pipeline yet?
		if (it->second.pipeline)
		{
			// Yes.
			return &it->second;
		}
		// No, it is still building.
		return nullptr;
	}
	else
	{
		// We haven't seen this before. Build it on the worker thread.

		struct TaskData
		{
			std::unique_ptr<GraphicsPipelineBuilder> builder;
			std::unique_ptr<VulkanPipeline> pipeline;
		};

		auto data = std::make_shared<TaskData>();

		// Compile the GLSL on the main thread (mainly due to the resource system)
		UniformStructHolder uniforms;
		data->builder = CreatePipeline(key, false, uniforms);
		SpecializedPipelines[key].Uniforms = uniforms;

		// Schedule the pipeline building on a worker thread
		VkPipelineKey k = key;
		auto passManager = fb->GetRenderPassManager();
		passManager->RunOnWorkerThread([=]() {

			cycle_t ct;
			ct.ResetAndClock();
			data->pipeline = data->builder->Create(fb->GetDevice());
			ct.Unclock();
			const auto duration = ct.TimeMS();

			passManager->RunOnMainThread([=]() {
				auto& slot = SpecializedPipelines[k];
				if (!slot.pipeline)
					slot.pipeline = std::move(data->pipeline);

				pipeline_time += duration;
				++pipeline_count;

				if (vk_debug_pipeline_creation)
				{
					Printf(">>> Pipeline created in %.3fms (Specialized worker)\n", duration);
				}
				});
			}, false);

		return nullptr;
	}
}

PipelineData* VkRenderPassSetup::GetGeneralizedPipeline(const VkPipelineKey& key)
{
	VkPipelineKey gkey = key;
	gkey.ShaderKey.AsQWORD = 0;

	auto item = GeneralizedPipelines.find(gkey);
	if (item == GeneralizedPipelines.end())
	{
		UniformStructHolder uniforms;
		auto pipeline = LinkPipeline(gkey, true, uniforms);
		auto ptr = pipeline.get();
		auto& value = GeneralizedPipelines[gkey];
		value.pipeline = std::move(pipeline);
		value.Uniforms = uniforms;
		return &value;
	}
	else
	{
		return &item->second;
	}
}

std::unique_ptr<VulkanPipeline> VkRenderPassSetup::CreateWithStats(GraphicsPipelineBuilder& builder, const char* type)
{
	cycle_t ct;
	ct.ResetAndClock();

	auto pipeline = builder.Create(fb->GetDevice());

	ct.Unclock();
	const auto duration = ct.TimeMS();
	pipeline_time += duration;
	++pipeline_count;

	if (vk_debug_pipeline_creation)
	{
		Printf(">>> Pipeline created in %.3fms (%s)\n", duration, type);
	}
	return pipeline;
}

std::unique_ptr<GraphicsPipelineBuilder> VkRenderPassSetup::CreatePipeline(const VkPipelineKey& key, bool isUberShader, UniformStructHolder& Uniforms)
{
	VkShaderProgram* program = fb->GetShaderManager()->Get(key.ShaderKey, isUberShader);

	Uniforms.Clear();
	Uniforms = program->Uniforms;

	auto builder = std::make_unique<GraphicsPipelineBuilder>();
	builder->Cache(fb->GetRenderPassManager()->GetCache());
	builder->Layout(fb->GetRenderPassManager()->GetPipelineLayout(key.ShaderKey.Layout.UseLevelMesh, program->Uniforms.sz));
	builder->RenderPass(GetRenderPass(0));
	builder->DebugName("VkRenderPassSetup.Pipeline");

	AddVertexInputInterface(*builder, key.ShaderKey.VertexFormat, key.DrawType);
	AddPreRasterizationShaders(*builder, key, program);
	AddFragmentShader(*builder, key, program);
	AddFragmentOutputInterface(*builder, key.RenderStyle, (VkColorComponentFlags)key.ColorMask);
	AddDynamicState(*builder);

	return builder;
}

VulkanPipeline* VkRenderPassSetup::GetVertexInputLibrary(int vertexFormat, int drawType, bool useLevelMesh, int userUniformSize)
{
	uint64_t key =
		(static_cast<uint64_t>(vertexFormat)) |
		(static_cast<uint64_t>(drawType) << 16) |
		(static_cast<uint64_t>(useLevelMesh) << 31) |
		(static_cast<uint64_t>(userUniformSize) << 32);

	auto& pipeline = Libraries.VertexInput[key];
	if (!pipeline)
		pipeline = CreateVertexInputLibrary(vertexFormat, drawType, useLevelMesh, userUniformSize);
	return pipeline.get();
}

VulkanPipeline* VkRenderPassSetup::GetFragmentOutputLibrary(FRenderStyle renderStyle, VkColorComponentFlags colorMask)
{
	uint64_t key = static_cast<uint64_t>(renderStyle.AsDWORD) | (static_cast<uint64_t>(colorMask) << 32);
	auto& pipeline = Libraries.FragmentOutput[key];
	if (!pipeline)
		pipeline = CreateFragmentOutputLibrary(renderStyle, colorMask);
	return pipeline.get();
}

VulkanPipeline* VkRenderPassSetup::GetVertexShaderLibrary(const VkPipelineKey& key, bool isUberShader)
{
	VkPipelineKey vkey = key;
	vkey.IsGeneralized = isUberShader;
	vkey.DrawType = 0;
	vkey.ColorMask = 0;
	vkey.DepthWrite = 0;
	vkey.DepthTest = 0;
	vkey.DepthFunc = 0;
	vkey.StencilTest = 0;
	vkey.StencilPassOp = 0;
	vkey.RenderStyle.AsDWORD = 0;
	if (isUberShader)
	{
		vkey.ShaderKey.AsQWORD = 0;
	}
	else
	{
		vkey.ShaderKey.TextureMode = 0;
		vkey.ShaderKey.ClampY = 0;
		vkey.ShaderKey.Brightmap = 0;
		vkey.ShaderKey.Detailmap = 0;
		vkey.ShaderKey.Glowmap = 0;
		vkey.ShaderKey.FogBeforeLights = 0;
		vkey.ShaderKey.FogAfterLights = 0;
		vkey.ShaderKey.FogRadial = 0;
		vkey.ShaderKey.SWLightRadial = 0;
		vkey.ShaderKey.SWLightBanded = 0;
		vkey.ShaderKey.LightMode = 0;
		vkey.ShaderKey.LightBlendMode = 0;
		vkey.ShaderKey.LightAttenuationMode = 0;
		vkey.ShaderKey.FogBalls = 0;
		vkey.ShaderKey.NoFragmentShader = 0;
		vkey.ShaderKey.DepthFadeThreshold = 0;
		vkey.ShaderKey.AlphaTestOnly = 0;
		vkey.ShaderKey.UseSpriteCenter = 0;
	}
	auto& pipeline = Libraries.VertexShader[vkey];
	if (!pipeline)
		pipeline = CreateVertexShaderLibrary(key, isUberShader);
	return pipeline.get();
}

VkPipelineKey VkRenderPassSetup::GetFragmentShaderKey(const VkPipelineKey& key, bool isUberShader)
{
	VkPipelineKey fkey = key;
	fkey.IsGeneralized = isUberShader;
	fkey.DrawLine = 0;
	fkey.DrawType = 0;
	fkey.CullMode = 0;
	fkey.ColorMask = 0;
	fkey.DepthClamp = 0;
	fkey.DepthBias = 0;
	fkey.RenderStyle.AsDWORD = 0;
	fkey.ShaderKey.VertexFormat = 0;
	if (isUberShader)
		fkey.ShaderKey.AsQWORD = 0;
	return fkey;
}

void VkRenderPassSetup::PrecompileFragmentShaderLibrary(const VkPipelineKey& key, bool isUberShader)
{
	VkPipelineKey fkey = GetFragmentShaderKey(key, isUberShader);
	auto it = Libraries.FragmentShader.find(fkey);
	if (it == Libraries.FragmentShader.end())
	{
		Libraries.FragmentShader[fkey] = nullptr;

		struct TaskData
		{
			std::unique_ptr<GraphicsPipelineBuilder> builder;
			std::unique_ptr<VulkanPipeline> pipeline;
		};

		auto data = std::make_shared<TaskData>();
		data->builder = CreateFragmentShaderLibrary(key, isUberShader);

		auto passManager = fb->GetRenderPassManager();
		passManager->RunOnWorkerThread([=]() {

			cycle_t ct;
			ct.ResetAndClock();
			data->pipeline = data->builder->Create(fb->GetDevice());
			ct.Unclock();
			const auto duration = ct.TimeMS();

			passManager->RunOnMainThread([=]() {
				auto& slot = Libraries.FragmentShader[fkey];
				if (!slot)
					slot = std::move(data->pipeline);

					pipeline_time += duration;
					++pipeline_count;

					if (vk_debug_pipeline_creation)
					{
						Printf(">>> Pipeline created in %.3fms (FragmentShaderLibrary worker)\n", duration);
					}
				});
			}, true);
	}
}

VulkanPipeline* VkRenderPassSetup::GetFragmentShaderLibrary(const VkPipelineKey& key, bool isUberShader)
{
	VkPipelineKey fkey = GetFragmentShaderKey(key, isUberShader);
	auto& pipeline = Libraries.FragmentShader[fkey];
	if (!pipeline)
		pipeline = CreateWithStats(*CreateFragmentShaderLibrary(key, isUberShader), "FragmentShaderLibrary");
	return pipeline.get();
}

std::unique_ptr<VulkanPipeline> VkRenderPassSetup::LinkPipeline(const VkPipelineKey& key, bool isUberShader, UniformStructHolder& Uniforms)
{
	VkShaderProgram* program = fb->GetShaderManager()->Get(key.ShaderKey, isUberShader);

	Uniforms.Clear();
	Uniforms = program->Uniforms;

	GraphicsPipelineBuilder builder;
	builder.Cache(fb->GetRenderPassManager()->GetCache());
	builder.AddLibrary(GetVertexInputLibrary(key.ShaderKey.VertexFormat, key.DrawType, key.ShaderKey.Layout.UseLevelMesh, program->Uniforms.sz));
	builder.AddLibrary(GetVertexShaderLibrary(key, isUberShader));
	builder.AddLibrary(GetFragmentShaderLibrary(key, isUberShader));
	builder.AddLibrary(GetFragmentOutputLibrary(key.RenderStyle, (VkColorComponentFlags)key.ColorMask));
	return CreateWithStats(builder, "Generalized");
}

std::unique_ptr<VulkanPipeline> VkRenderPassSetup::CreateVertexInputLibrary(int vertexFormat, int drawType, bool useLevelMesh, int userUniformSize)
{
	GraphicsPipelineBuilder builder;
	builder.Cache(fb->GetRenderPassManager()->GetCache());
	builder.Flags(VK_PIPELINE_CREATE_LIBRARY_BIT_KHR);
	builder.LibraryFlags(VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT);
	builder.Layout(fb->GetRenderPassManager()->GetPipelineLayout(useLevelMesh, userUniformSize));
	builder.RenderPass(GetRenderPass(0));
	builder.DebugName("VkRenderPassSetup.VertexInputLibrary");
	AddVertexInputInterface(builder, vertexFormat, drawType);
	AddDynamicState(builder);
	return CreateWithStats(builder, "VertexInputLibrary");
}

std::unique_ptr<VulkanPipeline> VkRenderPassSetup::CreateVertexShaderLibrary(const VkPipelineKey& key, bool isUberShader)
{
	VkShaderProgram* program = fb->GetShaderManager()->Get(key.ShaderKey, isUberShader);
	GraphicsPipelineBuilder builder;
	builder.Cache(fb->GetRenderPassManager()->GetCache());
	builder.Flags(VK_PIPELINE_CREATE_LIBRARY_BIT_KHR);
	builder.LibraryFlags(VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT);
	builder.Layout(fb->GetRenderPassManager()->GetPipelineLayout(key.ShaderKey.Layout.UseLevelMesh, program->Uniforms.sz));
	builder.RenderPass(GetRenderPass(0));
	builder.DebugName("VkRenderPassSetup.VertexShaderLibrary");
	AddPreRasterizationShaders(builder, key, program);
	AddDynamicState(builder);
	return CreateWithStats(builder, "VertexShaderLibrary");
}

std::unique_ptr<GraphicsPipelineBuilder> VkRenderPassSetup::CreateFragmentShaderLibrary(const VkPipelineKey& key, bool isUberShader)
{
	VkShaderProgram* program = fb->GetShaderManager()->Get(key.ShaderKey, isUberShader);
	auto builder = std::make_unique<GraphicsPipelineBuilder>();
	builder->Cache(fb->GetRenderPassManager()->GetCache());
	builder->Flags(VK_PIPELINE_CREATE_LIBRARY_BIT_KHR);
	builder->LibraryFlags(VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT);
	builder->Layout(fb->GetRenderPassManager()->GetPipelineLayout(key.ShaderKey.Layout.UseLevelMesh, program->Uniforms.sz));
	builder->RenderPass(GetRenderPass(0));
	builder->DebugName("VkRenderPassSetup.FragmentShaderLibrary");
	AddFragmentShader(*builder, key, program);
	AddDynamicState(*builder);
	return builder;
}

std::unique_ptr<VulkanPipeline> VkRenderPassSetup::CreateFragmentOutputLibrary(FRenderStyle renderStyle, VkColorComponentFlags colorMask)
{
	GraphicsPipelineBuilder builder;
	builder.Cache(fb->GetRenderPassManager()->GetCache());
	builder.Flags(VK_PIPELINE_CREATE_LIBRARY_BIT_KHR);
	builder.LibraryFlags(VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT);
	builder.RenderPass(GetRenderPass(0));
	builder.DebugName("VkRenderPassSetup.FragmentOutputLibrary");
	AddFragmentOutputInterface(builder, renderStyle, colorMask);
	AddDynamicState(builder);
	return CreateWithStats(builder, "FragmentOutputLibrary");
}

void VkRenderPassSetup::AddVertexInputInterface(GraphicsPipelineBuilder& builder, int vertexFormat, int drawType)
{
	const VkVertexFormat& vfmt = *fb->GetRenderPassManager()->GetVertexFormat(vertexFormat);

	for (int i = 0; i < vfmt.BufferStrides.size(); i++)
		builder.AddVertexBufferBinding(i, vfmt.BufferStrides[i]);

	const static VkFormat vkfmts[] = {
		VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_FORMAT_R32G32B32_SFLOAT,
		VK_FORMAT_R32G32_SFLOAT,
		VK_FORMAT_R32_SFLOAT,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_A2B10G10R10_SNORM_PACK32,
		VK_FORMAT_R8G8B8A8_UINT,
		VK_FORMAT_R32_SINT
	};

	for (size_t i = 0; i < vfmt.Attrs.size(); i++)
	{
		const auto& attr = vfmt.Attrs[i];
		builder.AddVertexAttribute(attr.location, attr.binding, vkfmts[attr.format], attr.offset);
	}

	static const VkPrimitiveTopology vktopology[] = {
		VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
	};
	builder.Topology(vktopology[drawType]);
}

void VkRenderPassSetup::AddPreRasterizationShaders(GraphicsPipelineBuilder& builder, const VkPipelineKey& key, VkShaderProgram* program)
{
	builder.PolygonMode(key.DrawLine ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL);
	builder.AddVertexShader(program->vert.get());
	builder.AddConstant(0, (uint32_t)key.ShaderKey.AsQWORD);
	builder.AddConstant(1, (uint32_t)(key.ShaderKey.AsQWORD >> 32));

	// Note: CCW and CW is intentionally swapped here because the vulkan and opengl coordinate systems differ.
	// main.vp addresses this by patching up gl_Position.z, which has the side effect of flipping the sign of the front face calculations.
	builder.Cull(key.CullMode == Cull_None ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT, key.CullMode == Cull_CW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);

	if (fb->GetDevice()->EnabledFeatures.Features.depthClamp)
		builder.DepthClampEnable(key.DepthClamp);
	builder.DepthBias(key.DepthBias, 0.0f, 0.0f, 0.0f);
}

void VkRenderPassSetup::AddFragmentShader(GraphicsPipelineBuilder& builder, const VkPipelineKey& key, VkShaderProgram* program)
{
	if (program->frag)
	{
		builder.AddFragmentShader(program->frag.get());
		builder.AddConstant(0, (uint32_t)key.ShaderKey.AsQWORD);
		builder.AddConstant(1, (uint32_t)(key.ShaderKey.AsQWORD >> 32));
	}

	static const VkStencilOp op2vk[] = { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_DECREMENT_AND_CLAMP };
	static const VkCompareOp depthfunc2vk[] = { VK_COMPARE_OP_LESS, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_ALWAYS };

	builder.DepthStencilEnable(key.DepthTest, key.DepthWrite, key.StencilTest);
	builder.DepthFunc(depthfunc2vk[key.DepthFunc]);
	builder.Stencil(VK_STENCIL_OP_KEEP, op2vk[key.StencilPassOp], VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0xffffffff, 0xffffffff, 0);
}

void VkRenderPassSetup::AddFragmentOutputInterface(GraphicsPipelineBuilder& builder, FRenderStyle renderStyle, VkColorComponentFlags colorMask)
{
	ColorBlendAttachmentBuilder blendbuilder;
	blendbuilder.ColorWriteMask(colorMask);
	BlendMode(blendbuilder, renderStyle);

	for (int i = 0; i < PassKey.DrawBuffers; i++)
		builder.AddColorBlendAttachment(blendbuilder.Create());

	builder.RasterizationSamples((VkSampleCountFlagBits)PassKey.Samples);
}

void VkRenderPassSetup::AddDynamicState(GraphicsPipelineBuilder& builder)
{
	builder.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
	builder.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR);
	builder.AddDynamicState(VK_DYNAMIC_STATE_DEPTH_BIAS);
	builder.AddDynamicState(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
}

/////////////////////////////////////////////////////////////////////////////

ColorBlendAttachmentBuilder& BlendMode(ColorBlendAttachmentBuilder& builder, const FRenderStyle& style)
{
	// Just in case Vulkan doesn't do this optimization itself
	if (style.BlendOp == STYLEOP_Add && style.SrcAlpha == STYLEALPHA_One && style.DestAlpha == STYLEALPHA_Zero && style.Flags == 0)
	{
		return builder;
	}

	static const int blendstyles[] = {
		VK_BLEND_FACTOR_ZERO,
		VK_BLEND_FACTOR_ONE,
		VK_BLEND_FACTOR_SRC_ALPHA,
		VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		VK_BLEND_FACTOR_SRC_COLOR,
		VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
		VK_BLEND_FACTOR_DST_COLOR,
		VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
		VK_BLEND_FACTOR_DST_ALPHA,
		VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	};

	static const int renderops[] = {
		0, VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
	};

	int srcblend = blendstyles[style.SrcAlpha % STYLEALPHA_MAX];
	int dstblend = blendstyles[style.DestAlpha % STYLEALPHA_MAX];
	int blendequation = renderops[style.BlendOp & 15];

	if (blendequation == -1)	// This was a fuzz style.
	{
		srcblend = VK_BLEND_FACTOR_DST_COLOR;
		dstblend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendequation = VK_BLEND_OP_ADD;
	}

	return builder.BlendMode((VkBlendOp)blendequation, (VkBlendFactor)srcblend, (VkBlendFactor)dstblend);
}
