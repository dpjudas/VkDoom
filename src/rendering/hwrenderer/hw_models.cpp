// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2005-2016 Christoph Oelckers
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//
/*
** gl_models.cpp
**
** hardware renderer model handling code
**
**/

#include "filesystem.h"
#include "g_game.h"
#include "doomstat.h"
#include "g_level.h"
#include "r_state.h"
#include "d_player.h"
#include "g_levellocals.h"
#include "i_time.h"
#include "cmdlib.h"
#include "hw_material.h"
#include "hwrenderer/data/buffers.h"
#include "flatvertices.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hw_renderstate.h"
#include "hwrenderer/scene/hw_portal.h"
#include "hw_models.h"
#include "hwrenderer/scene/hw_drawcontext.h"

CVAR(Bool, gl_light_models, true, CVAR_ARCHIVE)
EXTERN_CVAR(Bool, gl_texture);

VSMatrix FHWModelRenderer::GetViewToWorldMatrix()
{
	VSMatrix objectToWorldMatrix;
	di->VPUniforms.mViewMatrix.inverseMatrix(objectToWorldMatrix);
	return objectToWorldMatrix;
}

void FHWModelRenderer::BeginDrawModel(FRenderStyle style, int smf_flags, const VSMatrix &objectToWorldMatrix, bool mirrored)
{
	state.SetDepthFunc(DF_LEqual);
	state.EnableTexture(true);

	if (!gl_texture)
	{
		state.SetTextureMode(TM_STENCIL);
		state.SetRenderStyle(STYLE_Stencil);
	}

	// [BB] In case the model should be rendered translucent, do back face culling.
	// This solves a few of the problems caused by the lack of depth sorting.
	// [Nash] Don't do back face culling if explicitly specified in MODELDEF
	// TO-DO: Implement proper depth sorting.
	if ((smf_flags & MDL_FORCECULLBACKFACES) || (!(style == DefaultRenderStyle()) && !(smf_flags & MDL_DONTCULLBACKFACES)))
	{
		state.SetCulling((mirrored ^ di->drawctx->portalState.isMirrored()) ? Cull_CCW : Cull_CW);
	}

	VSMatrix normalModelMatrix;
	normalModelMatrix.computeNormalMatrix(objectToWorldMatrix);
	state.SetModelMatrix(objectToWorldMatrix, normalModelMatrix);
}

void FHWModelRenderer::EndDrawModel(FRenderStyle style, int smf_flags)
{
	state.SetBoneIndexBase(-1);
	state.SetModelMatrix(VSMatrix::identity(), VSMatrix::identity());
	state.SetDepthFunc(DF_Less);
	if ((smf_flags & MDL_FORCECULLBACKFACES) || (!(style == DefaultRenderStyle()) && !(smf_flags & MDL_DONTCULLBACKFACES)))
		state.SetCulling(Cull_None);
}

void FHWModelRenderer::BeginDrawHUDModel(FRenderStyle style, const VSMatrix &objectToWorldMatrix, bool mirrored, int smf_flags)
{
	state.SetDepthFunc(DF_LEqual);
	state.SetDepthClamp(true);

	state.EnableTexture(true);

	if (!gl_texture)
	{
		state.SetTextureMode(TM_STENCIL);
		state.SetRenderStyle(STYLE_Stencil);
	}

	// [BB] In case the model should be rendered translucent, do back face culling.
	// This solves a few of the problems caused by the lack of depth sorting.
	// TO-DO: Implement proper depth sorting.
	if (!(style == DefaultRenderStyle()) || (smf_flags & MDL_FORCECULLBACKFACES))
	{
		state.SetCulling((mirrored ^ di->drawctx->portalState.isMirrored()) ? Cull_CW : Cull_CCW);
	}

	VSMatrix normalModelMatrix;
	normalModelMatrix.computeNormalMatrix(objectToWorldMatrix);
	state.SetModelMatrix(objectToWorldMatrix, normalModelMatrix);
}

void FHWModelRenderer::EndDrawHUDModel(FRenderStyle style, int smf_flags)
{
	state.SetBoneIndexBase(-1);
	state.SetModelMatrix(VSMatrix::identity(), VSMatrix::identity());

	state.SetDepthFunc(DF_Less);
	if (!(style == DefaultRenderStyle()) || (smf_flags & MDL_FORCECULLBACKFACES))
		state.SetCulling(Cull_None);
}

IModelVertexBuffer *FHWModelRenderer::CreateVertexBuffer(bool needindex, bool singleframe)
{
	return new FModelVertexBuffer(needindex, singleframe);
}

void FHWModelRenderer::SetInterpolation(double inter)
{
	state.SetInterpolationFactor((float)inter);
}

void FHWModelRenderer::SetMaterial(FGameTexture *skin, bool clampNoFilter, FTranslationID translation, void * act_v)
{
	AActor * act = static_cast<AActor*>(act_v);

	state.SetMaterial(skin, UF_Skin, 0, clampNoFilter ? CLAMP_NOFILTER : CLAMP_NONE, translation, -1, act ? act->GetClass() : nullptr);

	int shader = state.getShaderIndex();

	if(shader >= FIRST_USER_SHADER && act && (act != lastAct || shader != lastShader))
	{ // only re-bind uniforms if the actor or the shader have changed
		usershaders[shader - FIRST_USER_SHADER].BindActorFields(act);
	}

	lastAct = act;
	lastShader = shader;

	state.SetLightIndex(modellightindex);
}

void FHWModelRenderer::DrawArrays(int start, int count)
{
	state.Draw(DT_Triangles, start, count);
}

void FHWModelRenderer::DrawElements(int numIndices, size_t offset)
{
	state.DrawIndexed(DT_Triangles, int(offset / sizeof(unsigned int)), numIndices);
}

//===========================================================================
//
//
//
//===========================================================================

void FHWModelRenderer::SetupFrame(FModel *model, unsigned int frame1, unsigned int frame2, unsigned int size, int boneStartIndex)
{
	auto mdbuff = static_cast<FModelVertexBuffer*>(model->GetVertexBuffer(GetType()));
	state.SetBoneIndexBase(boneStartIndex);
	if (mdbuff)
	{
		state.SetVertexBuffer(mdbuff->vertexBuffer(), frame1, frame2);
		if (mdbuff->indexBuffer()) state.SetIndexBuffer(mdbuff->indexBuffer());
	}
}

