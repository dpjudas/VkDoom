
#include "hw_meshbuilder.h"
#include "v_video.h"

MeshBuilder::MeshBuilder()
{
	Reset();

	// Initialize state same way as it begins in HWDrawInfo::RenderScene:
	SetTextureMode(TM_NORMAL);
	SetDepthMask(true);
	EnableFog(true);
	SetRenderStyle(STYLE_Source);
	SetDepthFunc(DF_Less);
	AlphaFunc(Alpha_GEqual, 0.f);
	ClearDepthBias();
	EnableTexture(1);
	EnableBrightmap(true);
}

void MeshBuilder::SetShadowData(const TArray<FFlatVertex>& vertices, const TArray<uint32_t>& indexes)
{
	mVertices = vertices;
	mIndexes = indexes;
}

void MeshBuilder::DoDraw(int dt, int index, int count, bool apply)
{
	if (apply)
		Apply();

	MeshDrawCommand command;
	command.DrawType = dt;
	command.Start = index;
	command.Count = count;
	command.ApplyIndex = -1;
	mDrawLists->mDraws.Push(command);
}

void MeshBuilder::DoDrawIndexed(int dt, int index, int count, bool apply)
{
	if (apply)
		Apply();

	MeshDrawCommand command;
	command.DrawType = dt;
	command.Start = index;
	command.Count = count;
	command.ApplyIndex = -1;
	mDrawLists->mIndexedDraws.Push(command);
}

void MeshBuilder::SetDepthFunc(int func)
{
	mDepthFunc = func;
}

void MeshBuilder::Apply()
{
	MeshApplyState state;

	state.vertexBuffer = static_cast<MeshBuilderBuffer<FModelVertex>*>(mVertexBuffer);
	state.indexBuffer = static_cast<MeshBuilderBuffer<unsigned int>*>(mIndexBuffer);
	state.applyData.RenderStyle = mRenderStyle;
	state.applyData.SpecialEffect = mSpecialEffect;
	state.applyData.TextureEnabled = mTextureEnabled;
	state.applyData.DepthFunc = mDepthFunc;
	state.applyData.FogEnabled = mFogEnabled;
	state.applyData.FogColor = (mFogColor & 0xffffff) == 0;
	state.applyData.BrightmapEnabled = mBrightmapEnabled;
	state.applyData.TextureClamp = mTextureClamp;
	state.applyData.TextureMode = mTextureMode;
	state.applyData.TextureModeFlags = mTextureModeFlags;
	state.surfaceUniforms = mSurfaceUniforms;
	state.material = mMaterial;
	state.textureMatrix = mTextureMatrix;

	mDrawLists = &mSortedLists[state];
}

std::pair<FFlatVertex*, unsigned int> MeshBuilder::AllocVertices(unsigned int count)
{
	unsigned int offset = mVertices.Reserve(count);
	return { &mVertices[offset], offset };
}

/////////////////////////////////////////////////////////////////////////////

MeshBuilderModelRender::MeshBuilderModelRender(MeshBuilder& renderstate) : renderstate(renderstate)
{
}

void MeshBuilderModelRender::BeginDrawModel(FRenderStyle style, int smf_flags, const VSMatrix& objectToWorldMatrix, bool mirrored)
{
	renderstate.EnableTexture(true);

	VSMatrix normalModelMatrix;
	normalModelMatrix.computeNormalMatrix(objectToWorldMatrix);
	renderstate.SetModelMatrix(objectToWorldMatrix, normalModelMatrix);
}

void MeshBuilderModelRender::EndDrawModel(FRenderStyle style, int smf_flags)
{
	renderstate.SetBoneIndexBase(-1);

	// Don't do this so we don't have to track matrix changes in MeshBuilder (only models uses matrices like this)
	//renderstate.SetModelMatrix(VSMatrix::identity(), VSMatrix::identity());
}

IModelVertexBuffer* MeshBuilderModelRender::CreateVertexBuffer(bool needindex, bool singleframe)
{
	return new MeshBuilderModelVertexBuffer();
}

VSMatrix MeshBuilderModelRender::GetViewToWorldMatrix()
{
	return VSMatrix::identity();
}

void MeshBuilderModelRender::BeginDrawHUDModel(FRenderStyle style, const VSMatrix& objectToWorldMatrix, bool mirrored, int smf_flags)
{
}

void MeshBuilderModelRender::EndDrawHUDModel(FRenderStyle style, int smf_flags)
{
}

void MeshBuilderModelRender::SetInterpolation(double interpolation)
{
	renderstate.SetInterpolationFactor((double)interpolation);
}

void MeshBuilderModelRender::SetMaterial(FGameTexture* skin, bool clampNoFilter, FTranslationID translation, void* act_v)
{
	renderstate.SetMaterial(skin, UF_Skin, 0, clampNoFilter ? CLAMP_NOFILTER : CLAMP_NONE, translation, -1, nullptr);
}

void MeshBuilderModelRender::DrawArrays(int start, int count)
{
	renderstate.Draw(DT_Triangles, start, count);
}

void MeshBuilderModelRender::DrawElements(int numIndices, size_t offset)
{
	renderstate.DrawIndexed(DT_Triangles, int(offset / sizeof(unsigned int)), numIndices);
}

void MeshBuilderModelRender::SetupFrame(FModel* model, unsigned int frame1, unsigned int frame2, unsigned int size, int boneStartIndex)
{
	auto mdbuff = static_cast<MeshBuilderModelVertexBuffer*>(model->GetVertexBuffer(GetType()));
	renderstate.SetBoneIndexBase(boneStartIndex);
	if (mdbuff)
	{
		renderstate.SetVertexBuffer(&mdbuff->vbuf, frame1, frame2);
		renderstate.SetIndexBuffer(&mdbuff->ibuf);
	}
}

int MeshBuilderModelRender::UploadBones(const TArray<VSMatrix>& bones)
{
	return renderstate.UploadBones(bones);
}
