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

#include "vk_renderstate.h"
#include "vulkan/vk_renderdevice.h"
#include "vulkan/vk_levelmesh.h"
#include "vulkan/commands/vk_commandbuffer.h"
#include "vulkan/buffers/vk_buffer.h"
#include "vulkan/pipelines/vk_renderpass.h"
#include "vulkan/descriptorsets/vk_descriptorset.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include "vulkan/textures/vk_hwtexture.h"
#include <zvulkan/vulkanbuilders.h>

#include "hw_skydome.h"
#include "hw_viewpointuniforms.h"
#include "hw_dynlightdata.h"
#include "hw_cvars.h"
#include "hw_clock.h"
#include "flatvertices.h"

#include "g_levellocals.h"

CVAR(Int, vk_submit_size, 1000, 0);
EXTERN_CVAR(Bool, r_skipmats)

VkRenderState::VkRenderState(VulkanRenderDevice* fb) : fb(fb), mRSBuffers(fb->GetBufferManager()->GetRSBuffers())
{
	mMatrices.ModelMatrix.loadIdentity();
	mMatrices.NormalModelMatrix.loadIdentity();
	mMatrices.TextureMatrix.loadIdentity();

	Reset();
}

void VkRenderState::ClearScreen()
{
	int width = fb->GetWidth();
	int height = fb->GetHeight();

	auto vertices = AllocVertices(4);
	FFlatVertex* v = vertices.first;
	v[0].Set(0, 0, 0, 0, 0);
	v[1].Set(0, (float)height, 0, 0, 1);
	v[2].Set((float)width, 0, 0, 1, 0);
	v[3].Set((float)width, (float)height, 0, 1, 1);

	Set2DViewpoint(width, height);
	SetColor(0, 0, 0);
	Apply(DT_TriangleStrip);

	mCommandBuffer->draw(4, 1, vertices.second, 0);
}

void VkRenderState::Draw(int dt, int index, int count, bool apply)
{
	if (apply || mNeedApply)
		Apply(dt);

	mCommandBuffer->draw(count, 1, index, 0);
}

void VkRenderState::DrawIndexed(int dt, int index, int count, bool apply)
{
	if (apply || mNeedApply)
		Apply(dt);

	mCommandBuffer->drawIndexed(count, 1, index, 0, 0);
}

bool VkRenderState::SetDepthClamp(bool on)
{
	bool lastValue = mDepthClamp;
	mDepthClamp = on;
	mNeedApply = true;
	return lastValue;
}

void VkRenderState::SetDepthMask(bool on)
{
	mDepthWrite = on;
	mNeedApply = true;
}

void VkRenderState::SetDepthFunc(int func)
{
	mDepthFunc = func;
	mNeedApply = true;
}

void VkRenderState::SetDepthRange(float min, float max)
{
	mViewportDepthMin = min;
	mViewportDepthMax = max;
	mViewportChanged = true;
	mNeedApply = true;
}

void VkRenderState::SetColorMask(bool r, bool g, bool b, bool a)
{
	int rr = r, gg = g, bb = b, aa = a;
	mColorMask = (aa << 3) | (bb << 2) | (gg << 1) | rr;
	mNeedApply = true;
}

void VkRenderState::SetStencil(int offs, int op, int flags)
{
	mStencilRef = screen->stencilValue + offs;
	mStencilRefChanged = true;
	mStencilOp = op;

	if (flags != -1)
	{
		bool cmon = !(flags & SF_ColorMaskOff);
		SetColorMask(cmon, cmon, cmon, cmon); // don't write to the graphics buffer
		mDepthWrite = !(flags & SF_DepthMaskOff);
	}

	mNeedApply = true;
}

void VkRenderState::SetCulling(int mode)
{
	mCullMode = mode;
	mNeedApply = true;
}

void VkRenderState::Clear(int targets)
{
	mClearTargets = targets;
	EndRenderPass();
}

void VkRenderState::EnableStencil(bool on)
{
	mStencilTest = on;
	mNeedApply = true;
}

void VkRenderState::SetScissor(int x, int y, int w, int h)
{
	mScissorX = x;
	mScissorY = y;
	mScissorWidth = w;
	mScissorHeight = h;
	mScissorChanged = true;
	mNeedApply = true;
}

void VkRenderState::SetViewport(int x, int y, int w, int h)
{
	mViewportX = x;
	mViewportY = y;
	mViewportWidth = w;
	mViewportHeight = h;
	mViewportChanged = true;
	mNeedApply = true;
}

void VkRenderState::EnableDepthTest(bool on)
{
	mDepthTest = on;
	mNeedApply = true;
}

void VkRenderState::EnableLineSmooth(bool on)
{
}

void VkRenderState::Apply(int dt)
{
	drawcalls.Clock();

	mApplyCount++;
	if (mApplyCount >= vk_submit_size)
	{
		fb->GetCommands()->FlushCommands(false);
		mApplyCount = 0;
	}

	ApplySurfaceUniforms();
	ApplyMatrices();
	ApplyRenderPass(dt);
	ApplyScissor();
	ApplyViewport();
	ApplyStencilRef();
	ApplyDepthBias();
	ApplyPushConstants();
	ApplyVertexBuffers();
	ApplyBufferSets();
	mNeedApply = false;

	drawcalls.Unclock();
}

void VkRenderState::ApplyDepthBias()
{
	if (mBias.mChanged)
	{
		mCommandBuffer->setDepthBias(mBias.mUnits, 0.0f, mBias.mFactor);
		mBias.mChanged = false;
	}
}

void VkRenderState::ApplyRenderPass(int dt)
{
	// Find a pipeline that matches our state
	VkPipelineKey pipelineKey;
	pipelineKey.DrawType = dt;
	pipelineKey.VertexFormat = mVertexBuffer ? static_cast<VkHardwareVertexBuffer*>(mVertexBuffer)->VertexFormat : mRSBuffers->Flatbuffer.VertexFormat;
	pipelineKey.RenderStyle = mRenderStyle;
	pipelineKey.DepthTest = mDepthTest;
	pipelineKey.DepthWrite = mDepthTest && mDepthWrite;
	pipelineKey.DepthFunc = mDepthFunc;
	pipelineKey.DepthClamp = mDepthClamp;
	pipelineKey.DepthBias = !(mBias.mFactor == 0 && mBias.mUnits == 0);
	pipelineKey.StencilTest = mStencilTest;
	pipelineKey.StencilPassOp = mStencilOp;
	pipelineKey.ColorMask = mColorMask;
	pipelineKey.CullMode = mCullMode;
	if (mSpecialEffect > EFF_NONE)
	{
		pipelineKey.ShaderKey.SpecialEffect = mSpecialEffect;
		pipelineKey.ShaderKey.EffectState = 0;
		pipelineKey.ShaderKey.AlphaTest = false;
	}
	else
	{
		int effectState = mMaterial.mOverrideShader >= 0 ? mMaterial.mOverrideShader : (mMaterial.mMaterial ? mMaterial.mMaterial->GetShaderIndex() : 0);
		pipelineKey.ShaderKey.SpecialEffect = EFF_NONE;
		pipelineKey.ShaderKey.EffectState = mTextureEnabled ? effectState : SHADER_NoTexture;
		if (r_skipmats && pipelineKey.ShaderKey.EffectState >= 3 && pipelineKey.ShaderKey.EffectState <= 4)
			pipelineKey.ShaderKey.EffectState = 0;
		pipelineKey.ShaderKey.AlphaTest = mSurfaceUniforms.uAlphaThreshold >= 0.f;
	}

	int uTextureMode = GetTextureModeAndFlags((mMaterial.mMaterial && mMaterial.mMaterial->Source()->isHardwareCanvas()) ? TM_OPAQUE : TM_NORMAL);
	pipelineKey.ShaderKey.TextureMode = uTextureMode & 0xffff;
	pipelineKey.ShaderKey.ClampY = (uTextureMode & TEXF_ClampY) != 0;
	pipelineKey.ShaderKey.Brightmap = (uTextureMode & TEXF_Brightmap) != 0;
	pipelineKey.ShaderKey.Detailmap = (uTextureMode & TEXF_Detailmap) != 0;
	pipelineKey.ShaderKey.Glowmap = (uTextureMode & TEXF_Glowmap) != 0;

	pipelineKey.ShaderKey.DepthFadeThreshold = mSurfaceUniforms.uDepthFadeThreshold > 0.0f;

	// The way GZDoom handles state is just plain insanity!
	int fogset = 0;
	if (mFogEnabled)
	{
		if (mFogEnabled == 2)
		{
			fogset = -3;	// 2D rendering with 'foggy' overlay.
		}
		else if ((mFogColor & 0xffffff) == 0)
		{
			fogset = gl_fogmode;
		}
		else
		{
			fogset = -gl_fogmode;
		}
	}
	pipelineKey.ShaderKey.Simple2D = (fogset == -3);
	pipelineKey.ShaderKey.FogBeforeLights = (fogset > 0);
	pipelineKey.ShaderKey.FogAfterLights = (fogset < 0);
	pipelineKey.ShaderKey.FogRadial = (fogset < -1 || fogset > 1);
	pipelineKey.ShaderKey.SWLightRadial = (gl_fogmode == 2);
	pipelineKey.ShaderKey.SWLightBanded = false; // gl_bandedswlight;
	pipelineKey.ShaderKey.FogBalls = mFogballIndex >= 0;

	float lightlevel = mSurfaceUniforms.uLightLevel;
	if (lightlevel < 0.0)
	{
		pipelineKey.ShaderKey.LightMode = 0; // Default
	}
	else
	{
		if (mLightMode == 5)
			pipelineKey.ShaderKey.LightMode = 3; // Build
		else if (mLightMode == 16)
			pipelineKey.ShaderKey.LightMode = 2; // Vanilla
		else
			pipelineKey.ShaderKey.LightMode = 1; // Software
	}

	pipelineKey.ShaderKey.UseShadowmap = gl_light_shadows == 1;
	pipelineKey.ShaderKey.UseRaytrace = gl_light_shadows == 2;

	pipelineKey.ShaderKey.GBufferPass = mRenderTarget.DrawBuffers > 1;

	pipelineKey.ShaderKey.LightBlendMode = (level.info ? static_cast<int>(level.info->lightblendmode) : 0);
	pipelineKey.ShaderKey.LightAttenuationMode = (level.info ? static_cast<int>(level.info->lightattenuationmode) : 0);

	// Is this the one we already have?
	bool inRenderPass = mCommandBuffer;
	bool changingPipeline = (!inRenderPass) || (pipelineKey != mPipelineKey);

	if (!inRenderPass)
	{
		mCommandBuffer = fb->GetCommands()->GetDrawCommands();

		mScissorChanged = true;
		mViewportChanged = true;
		mStencilRefChanged = true;
		mBias.mChanged = true;

		BeginRenderPass(mCommandBuffer);
	}

	if (changingPipeline)
	{
		mCommandBuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, mPassSetup->GetPipeline(pipelineKey));
		mPipelineKey = pipelineKey;
	}
}

void VkRenderState::ApplyStencilRef()
{
	if (mStencilRefChanged)
	{
		mCommandBuffer->setStencilReference(VK_STENCIL_FRONT_AND_BACK, mStencilRef);
		mStencilRefChanged = false;
	}
}

void VkRenderState::ApplyScissor()
{
	if (mScissorChanged)
	{
		VkRect2D scissor;
		if (mScissorWidth >= 0)
		{
			int x0 = clamp(mScissorX, 0, mRenderTarget.Width);
			int y0 = clamp(mScissorY, 0, mRenderTarget.Height);
			int x1 = clamp(mScissorX + mScissorWidth, 0, mRenderTarget.Width);
			int y1 = clamp(mScissorY + mScissorHeight, 0, mRenderTarget.Height);

			scissor.offset.x = x0;
			scissor.offset.y = y0;
			scissor.extent.width = x1 - x0;
			scissor.extent.height = y1 - y0;
		}
		else
		{
			scissor.offset.x = 0;
			scissor.offset.y = 0;
			scissor.extent.width = mRenderTarget.Width;
			scissor.extent.height = mRenderTarget.Height;
		}
		mCommandBuffer->setScissor(0, 1, &scissor);
		mScissorChanged = false;
	}
}

void VkRenderState::ApplyViewport()
{
	if (mViewportChanged)
	{
		VkViewport viewport;
		if (mViewportWidth >= 0)
		{
			viewport.x = (float)mViewportX;
			viewport.y = (float)mViewportY;
			viewport.width = (float)mViewportWidth;
			viewport.height = (float)mViewportHeight;
		}
		else
		{
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = (float)mRenderTarget.Width;
			viewport.height = (float)mRenderTarget.Height;
		}
		viewport.minDepth = mViewportDepthMin;
		viewport.maxDepth = mViewportDepthMax;
		mCommandBuffer->setViewport(0, 1, &viewport);
		mViewportChanged = false;
	}
}

void VkRenderState::ApplySurfaceUniforms()
{
	auto passManager = fb->GetRenderPassManager();

	mSurfaceUniforms.useVertexData = mVertexBuffer ? passManager->GetVertexFormat(static_cast<VkHardwareVertexBuffer*>(mVertexBuffer)->VertexFormat)->UseVertexData : 0;

	if (mMaterial.mMaterial && mMaterial.mMaterial->Source())
		mSurfaceUniforms.timer = static_cast<float>((double)(screen->FrameTime - firstFrame) * (double)mMaterial.mMaterial->Source()->GetShaderSpeed() / 1000.);
	else
		mSurfaceUniforms.timer = 0.0f;

	if (mMaterial.mChanged)
	{
		if (mMaterial.mMaterial)
		{
			auto source = mMaterial.mMaterial->Source();
			if (source->isHardwareCanvas())
				static_cast<FCanvasTexture*>(source->GetTexture())->NeedUpdate();

			mSurfaceUniforms.uTextureIndex = static_cast<VkMaterial*>(mMaterial.mMaterial)->GetBindlessIndex(mMaterial);
			mSurfaceUniforms.uSpecularMaterial = { source->GetGlossiness(), source->GetSpecularLevel() };
			mSurfaceUniforms.uDepthFadeThreshold = source->GetDepthFadeThreshold();
		}
		else
		{
			mSurfaceUniforms.uDepthFadeThreshold = 0.f;
			mSurfaceUniforms.uTextureIndex = 0;
		}
		mMaterial.mChanged = false;
	}

	if (!mRSBuffers->SurfaceUniformsBuffer->Write(mSurfaceUniforms))
	{
		WaitForStreamBuffers();
		mRSBuffers->SurfaceUniformsBuffer->Write(mSurfaceUniforms);
	}
}

void VkRenderState::ApplyPushConstants()
{
	mPushConstants.uDataIndex = mRSBuffers->SurfaceUniformsBuffer->DataIndex();
	mPushConstants.uLightIndex = mLightIndex >= 0 ? (mLightIndex % MAX_LIGHT_DATA) : -1;
	mPushConstants.uBoneIndexBase = mBoneIndexBase;
	mPushConstants.uFogballIndex = mFogballIndex >= 0 ? (mFogballIndex % MAX_FOGBALL_DATA) : -1;

	mCommandBuffer->pushConstants(fb->GetRenderPassManager()->GetPipelineLayout(mPipelineKey.ShaderKey.UseLevelMesh), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, (uint32_t)sizeof(PushConstants), &mPushConstants);
}

void VkRenderState::ApplyMatrices()
{
	if (mMatricesChanged)
	{
		if (!mRSBuffers->MatrixBuffer->Write(mMatrices))
		{
			WaitForStreamBuffers();
			mRSBuffers->MatrixBuffer->Write(mMatrices);
		}
		mMatricesChanged = false;
	}
}

void VkRenderState::ApplyVertexBuffers()
{
	if ((mVertexBuffer != mLastVertexBuffer || mVertexOffsets[0] != mLastVertexOffsets[0] || mVertexOffsets[1] != mLastVertexOffsets[1]))
	{
		// Note: second [0] for BufferStrides is not a typo. Not all the vertex formats have a second buffer and the entire thing assumes they have the same stride anyway.
		if (mVertexBuffer)
		{
			auto vkbuf = static_cast<VkHardwareVertexBuffer*>(mVertexBuffer);
			const VkVertexFormat* format = fb->GetRenderPassManager()->GetVertexFormat(vkbuf->VertexFormat);
			VkBuffer vertexBuffers[2] = { vkbuf->mBuffer->buffer, vkbuf->mBuffer->buffer };
			VkDeviceSize offsets[] = { mVertexOffsets[0] * format->BufferStrides[0], mVertexOffsets[1] * format->BufferStrides[0]};
			mCommandBuffer->bindVertexBuffers(0, 2, vertexBuffers, offsets);
		}
		else
		{
			const VkVertexFormat* format = fb->GetRenderPassManager()->GetVertexFormat(mRSBuffers->Flatbuffer.VertexFormat);
			VkBuffer vertexBuffers[2] = { mRSBuffers->Flatbuffer.VertexBuffer->buffer, mRSBuffers->Flatbuffer.VertexBuffer->buffer };
			VkDeviceSize offsets[] = { mVertexOffsets[0] * format->BufferStrides[0], mVertexOffsets[1] * format->BufferStrides[0]};
			mCommandBuffer->bindVertexBuffers(0, 2, vertexBuffers, offsets);
		}

		mLastVertexBuffer = mVertexBuffer;
		mLastVertexOffsets[0] = mVertexOffsets[0];
		mLastVertexOffsets[1] = mVertexOffsets[1];
	}

	if (mIndexBuffer != mLastIndexBuffer || mIndexBufferNeedsBind)
	{
		if (mIndexBuffer)
		{
			mCommandBuffer->bindIndexBuffer(static_cast<VkHardwareIndexBuffer*>(mIndexBuffer)->mBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
		}
		else
		{
			mCommandBuffer->bindIndexBuffer(mRSBuffers->Flatbuffer.IndexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
		}
		mLastIndexBuffer = mIndexBuffer;
		mIndexBufferNeedsBind = false;
	}
}

void VkRenderState::ApplyBufferSets()
{
	uint32_t matrixOffset = mRSBuffers->MatrixBuffer->Offset();
	uint32_t surfaceUniformsOffset = mRSBuffers->SurfaceUniformsBuffer->Offset();
	uint32_t lightsOffset = mLightIndex >= 0 ? (uint32_t)(mLightIndex / MAX_LIGHT_DATA) * sizeof(LightBufferUBO) : mLastLightsOffset;
	uint32_t fogballsOffset = mFogballIndex >= 0 ? (uint32_t)(mFogballIndex / MAX_FOGBALL_DATA) * sizeof(FogballBufferUBO) : mLastFogballsOffset;
	if (mViewpointOffset != mLastViewpointOffset || matrixOffset != mLastMatricesOffset || surfaceUniformsOffset != mLastSurfaceUniformsOffset || lightsOffset != mLastLightsOffset || fogballsOffset != mLastFogballsOffset)
	{
		auto descriptors = fb->GetDescriptorSetManager();
		VulkanPipelineLayout* layout = fb->GetRenderPassManager()->GetPipelineLayout(mPipelineKey.ShaderKey.UseLevelMesh);

		uint32_t offsets[5] = { mViewpointOffset, matrixOffset, surfaceUniformsOffset, lightsOffset, fogballsOffset };
		mCommandBuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, descriptors->GetFixedSet());
		mCommandBuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, descriptors->GetRSBufferSet(), 5, offsets);
		mCommandBuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 2, descriptors->GetBindlessSet());

		mLastViewpointOffset = mViewpointOffset;
		mLastMatricesOffset = matrixOffset;
		mLastSurfaceUniformsOffset = surfaceUniformsOffset;
		mLastLightsOffset = lightsOffset;
		mLastFogballsOffset = fogballsOffset;
	}
}

void VkRenderState::WaitForStreamBuffers()
{
	fb->WaitForCommands(false);
	mApplyCount = 0;
	mRSBuffers->SurfaceUniformsBuffer->Reset();
	mRSBuffers->MatrixBuffer->Reset();
	mMatricesChanged = true;
}

int VkRenderState::SetViewpoint(const HWViewpointUniforms& vp)
{
	if (mRSBuffers->Viewpoint.Count == mRSBuffers->Viewpoint.UploadIndex)
	{
		return mRSBuffers->Viewpoint.Count - 1;
	}
	memcpy(((char*)mRSBuffers->Viewpoint.Data) + mRSBuffers->Viewpoint.UploadIndex * mRSBuffers->Viewpoint.BlockAlign, &vp, sizeof(HWViewpointUniforms));
	int index = mRSBuffers->Viewpoint.UploadIndex++;
	mViewpointOffset = index * mRSBuffers->Viewpoint.BlockAlign;
	mNeedApply = true;
	return index;
}

void VkRenderState::SetViewpoint(int index)
{
	mViewpointOffset = index * mRSBuffers->Viewpoint.BlockAlign;
	mNeedApply = true;
}

void VkRenderState::SetModelMatrix(const VSMatrix& matrix, const VSMatrix& normalMatrix)
{
	mMatrices.ModelMatrix = matrix;
	mMatrices.NormalModelMatrix = normalMatrix;
	mMatricesChanged = true;
	mNeedApply = true;
}

void VkRenderState::SetTextureMatrix(const VSMatrix& matrix)
{
	mMatrices.TextureMatrix = matrix;
	mMatricesChanged = true;
	mNeedApply = true;
}

int VkRenderState::UploadLights(const FDynLightData& data)
{
	// All meaasurements here are in vec4's.
	int size0 = data.arrays[0].Size() / 4;
	int size1 = data.arrays[1].Size() / 4;
	int size2 = data.arrays[2].Size() / 4;
	int totalsize = size0 + size1 + size2 + 1;

	// Clamp lights so they aren't bigger than what fits into a single dynamic uniform buffer page
	if (totalsize > MAX_LIGHT_DATA)
	{
		int diff = totalsize - MAX_LIGHT_DATA;

		size2 -= diff;
		if (size2 < 0)
		{
			size1 += size2;
			size2 = 0;
		}
		if (size1 < 0)
		{
			size0 += size1;
			size1 = 0;
		}
		totalsize = size0 + size1 + size2 + 1;
	}

	// Check if we still have any lights
	if (totalsize <= 1)
		return -1;

	// Make sure the light list doesn't cross a page boundary
	if (mRSBuffers->Lightbuffer.UploadIndex % MAX_LIGHT_DATA + totalsize > MAX_LIGHT_DATA)
		mRSBuffers->Lightbuffer.UploadIndex = (mRSBuffers->Lightbuffer.UploadIndex / MAX_LIGHT_DATA + 1) * MAX_LIGHT_DATA;

	int thisindex = mRSBuffers->Lightbuffer.UploadIndex;
	if (thisindex + totalsize <= mRSBuffers->Lightbuffer.Count)
	{
		mRSBuffers->Lightbuffer.UploadIndex += totalsize;

		float parmcnt[] = { 0, float(size0), float(size0 + size1), float(size0 + size1 + size2) };

		float* copyptr = (float*)mRSBuffers->Lightbuffer.Data + thisindex * 4;
		memcpy(&copyptr[0], parmcnt, sizeof(FVector4));
		memcpy(&copyptr[4], &data.arrays[0][0], size0 * sizeof(FVector4));
		memcpy(&copyptr[4 + 4 * size0], &data.arrays[1][0], size1 * sizeof(FVector4));
		memcpy(&copyptr[4 + 4 * (size0 + size1)], &data.arrays[2][0], size2 * sizeof(FVector4));
		return thisindex;
	}
	else
	{
		return -1;	// Buffer is full. Since it is being used live at the point of the upload we cannot do much here but to abort.
	}
}

int VkRenderState::UploadBones(const TArray<VSMatrix>& bones)
{
	int totalsize = bones.Size();
	if (bones.Size() == 0)
	{
		return -1;
	}

	int thisindex = mRSBuffers->Bonebuffer.UploadIndex;
	mRSBuffers->Bonebuffer.UploadIndex += totalsize;

	if (thisindex + totalsize <= mRSBuffers->Bonebuffer.Count)
	{
		memcpy((VSMatrix*)mRSBuffers->Bonebuffer.Data + thisindex, bones.Data(), bones.Size() * sizeof(VSMatrix));
		return thisindex;
	}
	else
	{
		return -1;	// Buffer is full. Since it is being used live at the point of the upload we cannot do much here but to abort.
	}
}

int VkRenderState::UploadFogballs(const TArray<Fogball>& balls)
{
	int totalsize = balls.Size() + 1;
	if (balls.Size() == 0)
	{
		return -1;
	}

	// Make sure the fogball list doesn't cross a page boundary
	if (mRSBuffers->Fogballbuffer.UploadIndex % MAX_FOGBALL_DATA + totalsize > MAX_FOGBALL_DATA)
		mRSBuffers->Fogballbuffer.UploadIndex = (mRSBuffers->Fogballbuffer.UploadIndex / MAX_FOGBALL_DATA + 1) * MAX_FOGBALL_DATA;

	int thisindex = mRSBuffers->Fogballbuffer.UploadIndex;
	mRSBuffers->Fogballbuffer.UploadIndex += totalsize;

	if (thisindex + totalsize <= mRSBuffers->Fogballbuffer.Count)
	{
		Fogball sizeinfo; // First entry is actually not a fogball. It is the size of the array.
		sizeinfo.Position.X = (float)balls.Size();
		memcpy((Fogball*)mRSBuffers->Fogballbuffer.Data + thisindex, &sizeinfo, sizeof(Fogball));
		memcpy((Fogball*)mRSBuffers->Fogballbuffer.Data + thisindex + 1, balls.Data(), balls.Size() * sizeof(Fogball));
		return thisindex;
	}
	else
	{
		return -1;
	}
}

std::pair<FFlatVertex*, unsigned int> VkRenderState::AllocVertices(unsigned int count)
{
	unsigned int index = mRSBuffers->Flatbuffer.CurIndex;
	if (index + count >= mRSBuffers->Flatbuffer.BUFFER_SIZE_TO_USE)
	{
		// If a single scene needs 2'000'000 vertices there must be something very wrong. 
		I_FatalError("Out of vertex memory. Tried to allocate more than %u vertices for a single frame", index + count);
	}
	mRSBuffers->Flatbuffer.CurIndex += count;
	return std::make_pair(mRSBuffers->Flatbuffer.Vertices + index, index);
}

void VkRenderState::SetShadowData(const TArray<FFlatVertex>& vertices, const TArray<uint32_t>& indexes)
{
	auto commands = fb->GetCommands();

	UpdateShadowData(0, vertices.Data(), vertices.Size());
	mRSBuffers->Flatbuffer.ShadowDataSize = vertices.Size();
	mRSBuffers->Flatbuffer.CurIndex = mRSBuffers->Flatbuffer.ShadowDataSize;

	if (indexes.Size() > 0)
	{
		size_t bufsize = indexes.Size() * sizeof(uint32_t);

		auto buffer = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY)
			.Size(bufsize)
			.DebugName("Flatbuffer.IndexBuffer")
			.Create(fb->GetDevice());

		auto staging = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
			.Size(bufsize)
			.DebugName("Flatbuffer.IndexBuffer.Staging")
			.Create(fb->GetDevice());

		void* dst = staging->Map(0, bufsize);
		memcpy(dst, indexes.Data(), bufsize);
		staging->Unmap();

		commands->GetTransferCommands()->copyBuffer(staging.get(), buffer.get());
		commands->TransferDeleteList->Add(std::move(staging));

		commands->DrawDeleteList->Add(std::move(mRSBuffers->Flatbuffer.IndexBuffer));
		mRSBuffers->Flatbuffer.IndexBuffer = std::move(buffer);

		mIndexBufferNeedsBind = true;
		mNeedApply = true;
	}
}

void VkRenderState::UpdateShadowData(unsigned int index, const FFlatVertex* vertices, unsigned int count)
{
	memcpy(mRSBuffers->Flatbuffer.Vertices + index, vertices, count * sizeof(FFlatVertex));
}

void VkRenderState::ResetVertices()
{
	mRSBuffers->Flatbuffer.CurIndex = mRSBuffers->Flatbuffer.ShadowDataSize;
}

void VkRenderState::BeginFrame()
{
	mMaterial.Reset();
	mApplyCount = 0;

	mRSBuffers->Viewpoint.UploadIndex = 0;
	mRSBuffers->Lightbuffer.UploadIndex = 0;
	mRSBuffers->Bonebuffer.UploadIndex = 0;
	mRSBuffers->Fogballbuffer.UploadIndex = 0;
	mRSBuffers->OcclusionQuery.NextIndex = 0;

	fb->GetCommands()->GetDrawCommands()->resetQueryPool(mRSBuffers->OcclusionQuery.QueryPool.get(), 0, mRSBuffers->OcclusionQuery.MaxQueries);
}

void VkRenderState::EndRenderPass()
{
	if (mCommandBuffer)
	{
		mCommandBuffer->endRenderPass();
		mCommandBuffer = nullptr;
	}

	// Force rebind of everything on next draw
	mPipelineKey = {};
	mLastViewpointOffset = 0xffffffff;
	mLastVertexOffsets[0] = 0xffffffff;
	mIndexBufferNeedsBind = true;
}

void VkRenderState::EndFrame()
{
	mRSBuffers->MatrixBuffer->Reset();
	mRSBuffers->SurfaceUniformsBuffer->Reset();
	mMatricesChanged = true;
}

void VkRenderState::EnableDrawBuffers(int count, bool apply)
{
	if (mRenderTarget.DrawBuffers != count)
	{
		EndRenderPass();
		mRenderTarget.DrawBuffers = count;
	}
}

void VkRenderState::SetRenderTarget(VkTextureImage *image, VulkanImageView *depthStencilView, int width, int height, VkFormat format, VkSampleCountFlagBits samples)
{
	EndRenderPass();

	mRenderTarget.Image = image;
	mRenderTarget.DepthStencil = depthStencilView;
	mRenderTarget.Width = width;
	mRenderTarget.Height = height;
	mRenderTarget.Format = format;
	mRenderTarget.Samples = samples;
}

void VkRenderState::BeginRenderPass(VulkanCommandBuffer *cmdbuffer)
{
	VkRenderPassKey key = {};
	key.DrawBufferFormat = mRenderTarget.Format;
	key.Samples = mRenderTarget.Samples;
	key.DrawBuffers = mRenderTarget.DrawBuffers;
	key.DepthStencil = !!mRenderTarget.DepthStencil;

	mPassSetup = fb->GetRenderPassManager()->GetRenderPass(key);

	auto &framebuffer = mRenderTarget.Image->RSFramebuffers[key];
	if (!framebuffer)
	{
		auto buffers = fb->GetBuffers();
		FramebufferBuilder builder;
		builder.RenderPass(mPassSetup->GetRenderPass(0));
		builder.Size(mRenderTarget.Width, mRenderTarget.Height);
		builder.AddAttachment(mRenderTarget.Image->View.get());
		if (key.DrawBuffers > 1)
			builder.AddAttachment(buffers->SceneFog.View.get());
		if (key.DrawBuffers > 2)
			builder.AddAttachment(buffers->SceneNormal.View.get());
		if (key.DepthStencil)
			builder.AddAttachment(mRenderTarget.DepthStencil);
		builder.DebugName("VkRenderPassSetup.Framebuffer");
		framebuffer = builder.Create(fb->GetDevice());
	}

	// Only clear depth+stencil if the render target actually has that
	if (!mRenderTarget.DepthStencil)
		mClearTargets &= ~(CT_Depth | CT_Stencil);

	RenderPassBegin beginInfo;
	beginInfo.RenderPass(mPassSetup->GetRenderPass(mClearTargets));
	beginInfo.RenderArea(0, 0, mRenderTarget.Width, mRenderTarget.Height);
	beginInfo.Framebuffer(framebuffer.get());
	beginInfo.AddClearColor(screen->mSceneClearColor[0], screen->mSceneClearColor[1], screen->mSceneClearColor[2], screen->mSceneClearColor[3]);
	if (key.DrawBuffers > 1)
		beginInfo.AddClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	if (key.DrawBuffers > 2)
		beginInfo.AddClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	beginInfo.AddClearDepthStencil(1.0f, 0);
	beginInfo.Execute(cmdbuffer);

	mMaterial.mChanged = true;
	mClearTargets = 0;
}

void VkRenderState::RaytraceScene(const FVector3& cameraPos, const VSMatrix& viewToWorld, float fovy, float aspect)
{
	ApplyMatrices();
	ApplyRenderPass(DT_Triangles);
	ApplyScissor();
	ApplyViewport();
	ApplyStencilRef();
	ApplyDepthBias();
	mNeedApply = true;

	VkRenderPassKey key = {};
	key.DrawBufferFormat = mRenderTarget.Format;
	key.Samples = mRenderTarget.Samples;
	key.DrawBuffers = mRenderTarget.DrawBuffers;
	key.DepthStencil = !!mRenderTarget.DepthStencil;
	fb->GetLevelMesh()->RaytraceScene(key, mCommandBuffer, cameraPos, viewToWorld, fovy, aspect);
}

void VkRenderState::ApplyLevelMesh()
{
	ApplyMatrices();
	ApplyRenderPass(DT_Triangles);
	ApplyScissor();
	ApplyViewport();
	ApplyStencilRef();
	ApplyDepthBias();
	mNeedApply = true;

	VkBuffer vertexBuffers[2] = { fb->GetLevelMesh()->GetVertexBuffer()->buffer, fb->GetLevelMesh()->GetUniformIndexBuffer()->buffer };
	VkDeviceSize vertexBufferOffsets[] = { 0, 0 };
	mCommandBuffer->bindVertexBuffers(0, 2, vertexBuffers, vertexBufferOffsets);
	mCommandBuffer->bindIndexBuffer(fb->GetLevelMesh()->GetDrawIndexBuffer()->buffer, 0, VK_INDEX_TYPE_UINT32);
}

void VkRenderState::RunZMinMaxPass()
{
	auto pipelines = fb->GetRenderPassManager();
	auto descriptors = fb->GetDescriptorSetManager();
	auto buffers = fb->GetBuffers();
	auto cmdbuffer = fb->GetCommands()->GetDrawCommands();

	fb->GetCommands()->PushGroup(cmdbuffer, "zminmax");

	int width = ((buffers->GetWidth() + 63) / 64 * 64) >> 1;
	int height = ((buffers->GetHeight() + 63) / 64 * 64) >> 1;

	ZMinMaxPushConstants pushConstants = {};
	pushConstants.LinearizeDepthA = 1.0f / screen->GetZFar() - 1.0f / screen->GetZNear();
	pushConstants.LinearizeDepthB = max(1.0f / screen->GetZNear(), 1.e-8f);
	pushConstants.InverseDepthRangeA = 1.0f;
	pushConstants.InverseDepthRangeB = 0.0f;

	VkImageTransition()
		.AddImage(&fb->GetBuffers()->SceneDepthStencil, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false)
		.AddImage(&fb->GetBuffers()->SceneZMinMax[0], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true)
		.Execute(cmdbuffer);

	RenderPassBegin()
		.RenderPass(pipelines->GetZMinMaxRenderPass())
		.RenderArea(0, 0, width, height)
		.Framebuffer(buffers->GetZMinMaxFramebuffer(0))
		.AddClearColor(0.0f, 0.0f, 0.0f, 0.0f)
		.Execute(cmdbuffer);

	VkViewport viewport = {};
	viewport.width = (float)width;
	viewport.height = (float)height;
	cmdbuffer->setViewport(0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.extent.width = width;
	scissor.extent.height = height;
	cmdbuffer->setScissor(0, 1, &scissor);

	cmdbuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->GetZMinMaxPipeline0(mRenderTarget.Samples));
	cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->GetZMinMaxLayout(), 0, descriptors->GetZMinMaxSet(0));
	cmdbuffer->pushConstants(pipelines->GetZMinMaxLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, (uint32_t)sizeof(ZMinMaxPushConstants), &pushConstants);
	cmdbuffer->draw(6, 1, 0, 0);
	cmdbuffer->endRenderPass();

	for (int i = 1; i < 6; i++)
	{
		VkImageTransition()
			.AddImage(&fb->GetBuffers()->SceneZMinMax[i - 1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false)
			.AddImage(&fb->GetBuffers()->SceneZMinMax[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true)
			.Execute(cmdbuffer);

		RenderPassBegin()
			.RenderPass(pipelines->GetZMinMaxRenderPass())
			.RenderArea(0, 0, width >> i, height >> i)
			.Framebuffer(buffers->GetZMinMaxFramebuffer(i))
			.AddClearColor(0.0f, 0.0f, 0.0f, 0.0f)
			.Execute(cmdbuffer);

		viewport = {};
		viewport.width = (float)(width >> i);
		viewport.height = (float)(height >> i);
		cmdbuffer->setViewport(0, 1, &viewport);

		scissor = {};
		scissor.extent.width = (width >> i);
		scissor.extent.height = (height >> i);
		cmdbuffer->setScissor(0, 1, &scissor);

		cmdbuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->GetZMinMaxPipeline1());
		cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->GetZMinMaxLayout(), 0, descriptors->GetZMinMaxSet(i));
		cmdbuffer->draw(6, 1, 0, 0);
		cmdbuffer->endRenderPass();
	}

	VkImageTransition()
		.AddImage(&fb->GetBuffers()->SceneDepthStencil, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false)
		.AddImage(&fb->GetBuffers()->SceneZMinMax[5], VK_IMAGE_LAYOUT_GENERAL, false)
		.Execute(cmdbuffer);

	fb->GetCommands()->PopGroup(cmdbuffer);
}

void VkRenderState::DispatchLightTiles(const VSMatrix& worldToView, float m5)
{
	EndRenderPass();
	RunZMinMaxPass();

	auto cmdbuffer = fb->GetCommands()->GetDrawCommands();

	fb->GetCommands()->PushGroup(cmdbuffer, "lighttiles");

	PipelineBarrier()
		.AddBuffer(fb->GetBuffers()->SceneLightTiles.get(), VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	float sceneWidth = (float)fb->GetBuffers()->GetSceneWidth();
	float sceneHeight = (float)fb->GetBuffers()->GetSceneHeight();
	float aspect = sceneWidth / sceneHeight;

	//float tanHalfFovy = tan(fovy * (M_PI / 360.0f));
	float tanHalfFovy = 1.0f / m5;
	float invFocalLenX = tanHalfFovy * aspect;
	float invFocalLenY = tanHalfFovy;

	LightTilesPushConstants pushConstants = {};
	pushConstants.posToViewA = { 2.0f * invFocalLenX / sceneWidth, 2.0f * invFocalLenY / sceneHeight };
	pushConstants.posToViewB = { -invFocalLenX, -invFocalLenY };
	pushConstants.viewportPos = { 0.0f, 0.0f };
	pushConstants.worldToView = worldToView;
	auto pipelines = fb->GetRenderPassManager();
	auto descriptors = fb->GetDescriptorSetManager();
	cmdbuffer->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->GetLightTilesPipeline());
	cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->GetLightTilesLayout(), 0, descriptors->GetLightTilesSet());
	cmdbuffer->pushConstants(pipelines->GetLightTilesLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, (uint32_t)sizeof(LightTilesPushConstants), &pushConstants);

	cmdbuffer->dispatch(
		(fb->GetBuffers()->GetWidth() + 63) / 64,
		(fb->GetBuffers()->GetHeight() + 63) / 64,
		1);

	PipelineBarrier()
		.AddBuffer(fb->GetBuffers()->SceneLightTiles.get(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	fb->GetCommands()->PopGroup(cmdbuffer);
}

void VkRenderState::DrawLevelMesh(LevelMeshDrawType drawType, bool noFragmentShader)
{
	ApplyLevelMesh();

	auto mesh = fb->GetLevelMesh()->GetMesh();
	for (auto& it : mesh->DrawList[(int)drawType])
	{
		int pipelineID = it.first;
		const VkPipelineKey& key = fb->GetLevelMeshPipelineKey(pipelineID);

		ApplyLevelMeshPipeline(mCommandBuffer, key, drawType, noFragmentShader);

		for (const MeshBufferRange& range : it.second.GetRanges())
		{
			mCommandBuffer->drawIndexed(range.End - range.Start, 1, range.Start, 0, 0);
		}
	}
}

void VkRenderState::BeginQuery()
{
	if (!mCommandBuffer)
		ApplyRenderPass(DT_Triangles);
	mCommandBuffer->beginQuery(mRSBuffers->OcclusionQuery.QueryPool.get(), mRSBuffers->OcclusionQuery.NextIndex++, 0);
}

void VkRenderState::EndQuery()
{
	mCommandBuffer->endQuery(mRSBuffers->OcclusionQuery.QueryPool.get(), mRSBuffers->OcclusionQuery.NextIndex - 1);
}

int VkRenderState::GetNextQueryIndex()
{
	return mRSBuffers->OcclusionQuery.NextIndex;
}

void VkRenderState::GetQueryResults(int queryStart, int queryCount, TArray<bool>& results)
{
	fb->GetCommands()->FlushCommands(false);

	mQueryResultsBuffer.Resize(queryCount);
	VkResult result = vkGetQueryPoolResults(fb->GetDevice()->device, mRSBuffers->OcclusionQuery.QueryPool->pool, queryStart, queryCount, mQueryResultsBuffer.Size() * sizeof(uint32_t), mQueryResultsBuffer.Data(), sizeof(uint32_t), VK_QUERY_RESULT_WAIT_BIT);
	CheckVulkanError(result, "Could not query occlusion query results");
	if (result == VK_NOT_READY)
		VulkanError("Occlusion query results returned VK_NOT_READY!");

	results.Resize(queryCount);
	for (int i = 0; i < queryCount; i++)
	{
		results[i] = mQueryResultsBuffer[i] != 0;
	}
}

void VkRenderState::ApplyLevelMeshPipeline(VulkanCommandBuffer* cmdbuffer, VkPipelineKey pipelineKey, LevelMeshDrawType drawType, bool noFragmentShader)
{
	if (drawType == LevelMeshDrawType::Masked && noFragmentShader)
	{
		// We unfortunately have to run the fragment shader to know which pixels are masked. Use a simplified version to reduce the cost.
		noFragmentShader = false;
		pipelineKey.ShaderKey.AlphaTestOnly = true;
	}

	// Global state that don't require rebuilding the mesh
	pipelineKey.ShaderKey.NoFragmentShader = noFragmentShader;
	pipelineKey.ShaderKey.UseShadowmap = gl_light_shadows == 1;
	pipelineKey.ShaderKey.UseRaytrace = gl_light_shadows == 2;
	pipelineKey.ShaderKey.GBufferPass = mRenderTarget.DrawBuffers > 1;

	// State overridden by the renderstate drawing the mesh
	pipelineKey.DepthTest = mDepthTest;
	pipelineKey.DepthWrite = mDepthTest && mDepthWrite;
	pipelineKey.DepthClamp = mDepthClamp;
	pipelineKey.DepthBias = !(mBias.mFactor == 0 && mBias.mUnits == 0);
	pipelineKey.StencilTest = mStencilTest;
	pipelineKey.StencilPassOp = mStencilOp;
	pipelineKey.ColorMask = mColorMask;
	pipelineKey.CullMode = mCullMode;
	if (!mTextureEnabled)
		pipelineKey.ShaderKey.EffectState = SHADER_NoTexture;

	mPipelineKey = pipelineKey;

	PushConstants pushConstants = {};
	pushConstants.uBoneIndexBase = -1;
	pushConstants.uFogballIndex = -1;

	VulkanPipelineLayout* layout = fb->GetRenderPassManager()->GetPipelineLayout(pipelineKey.ShaderKey.UseLevelMesh);
	uint32_t viewpointOffset = mViewpointOffset;
	uint32_t matrixOffset = mRSBuffers->MatrixBuffer->Offset();
	uint32_t fogballsOffset = 0;
	uint32_t offsets[] = { viewpointOffset, matrixOffset, fogballsOffset };

	auto descriptors = fb->GetDescriptorSetManager();
	cmdbuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, mPassSetup->GetPipeline(pipelineKey));
	cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, descriptors->GetFixedSet());
	cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, descriptors->GetLevelMeshSet(), 3, offsets);
	cmdbuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 2, descriptors->GetBindlessSet());
	cmdbuffer->pushConstants(layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, (uint32_t)sizeof(PushConstants), &pushConstants);
}

/////////////////////////////////////////////////////////////////////////////

void VkRenderStateMolten::Draw(int dt, int index, int count, bool apply)
{
	if (dt == DT_TriangleFan)
	{
		IBuffer* oldIndexBuffer = mIndexBuffer;
		mIndexBuffer = fb->GetBufferManager()->FanToTrisIndexBuffer.get();

		if (apply || mNeedApply)
			Apply(DT_Triangles);
		else
			ApplyVertexBuffers();

		mCommandBuffer->drawIndexed((count - 2) * 3, 1, 0, index, 0);

		mIndexBuffer = oldIndexBuffer;
	}
	else
	{
		if (apply || mNeedApply)
			Apply(dt);

		mCommandBuffer->draw(count, 1, index, 0);
	}
}
