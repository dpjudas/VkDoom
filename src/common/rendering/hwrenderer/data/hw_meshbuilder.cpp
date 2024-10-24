
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

void MeshBuilder::Draw(int dt, int index, int count, bool apply)
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

void MeshBuilder::DrawIndexed(int dt, int index, int count, bool apply)
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
