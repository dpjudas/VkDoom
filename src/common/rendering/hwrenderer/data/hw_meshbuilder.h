#pragma once

#include "hw_renderstate.h"
#include "hw_material.h"
#include "flatvertices.h"
#include "modelrenderer.h"
#include "buffers.h"
#include <map>

class Mesh;

template<typename T>
class MeshBuilderBuffer : public IBuffer
{
public:
	TArray<T> Data;

	void SetData(size_t size, const void* data, BufferUsageType type) override
	{
		Data.resize(size);
		memcpy(Data.data(), data, size);
	}

	void SetSubData(size_t offset, size_t size, const void* data) override
	{
		memcpy(reinterpret_cast<uint8_t*>(Data.data()) + offset, data, size);
	}

	void* Lock(unsigned int size) override
	{
		Data.Resize(size);
		return Data.data();
	}

	void Unlock() override {}
};

struct MeshApplyData
{
	FRenderStyle RenderStyle;
	int SpecialEffect;
	int TextureEnabled;
	int DepthFunc;
	int FogEnabled;
	int FogColor;
	int BrightmapEnabled;
	int TextureClamp;
	int TextureMode;
	int TextureModeFlags;
};

class MeshApplyState
{
public:
	MeshApplyData applyData;
	SurfaceUniforms surfaceUniforms;
	FMaterialState material;
	VSMatrix textureMatrix;
	MeshBuilderBuffer<FModelVertex>* vertexBuffer = nullptr;
	MeshBuilderBuffer<unsigned int>* indexBuffer = nullptr;

	bool operator<(const MeshApplyState& other) const
	{
		if (material.mMaterial != other.material.mMaterial)
			return material.mMaterial < other.material.mMaterial;
		if (material.mClampMode != other.material.mClampMode)
			return material.mClampMode < other.material.mClampMode;
		if (material.mTranslation != other.material.mTranslation)
			return material.mTranslation < other.material.mTranslation;
		if (material.mOverrideShader != other.material.mOverrideShader)
			return material.mOverrideShader < other.material.mOverrideShader;

		if (vertexBuffer != other.vertexBuffer)
			return vertexBuffer < other.vertexBuffer;
		if (indexBuffer != other.indexBuffer)
			return indexBuffer < other.indexBuffer;

		int result = memcmp(&applyData, &other.applyData, sizeof(MeshApplyData));
		if (result != 0)
			return result < 0;

		result = memcmp(&textureMatrix, &other.textureMatrix, sizeof(VSMatrix));
		if (result != 0)
			return result < 0;

		result = memcmp(&surfaceUniforms, &other.surfaceUniforms, sizeof(SurfaceUniforms));
		return result < 0;
	}
};

class MeshDrawCommand
{
public:
	int DrawType;
	int Start;
	int Count;
	int ApplyIndex;
};

class MeshBuilder : public FRenderState
{
public:
	MeshBuilder();

	// Vertices
	std::pair<FFlatVertex*, unsigned int> AllocVertices(unsigned int count) override;
	void SetShadowData(const TArray<FFlatVertex>& vertices, const TArray<uint32_t>& indexes) override;
	void UpdateShadowData(unsigned int index, const FFlatVertex* vertices, unsigned int count) override { }
	void ResetVertices() override { }

	// Buffers
	int SetViewpoint(const HWViewpointUniforms& vp) override { return 0; }
	void SetViewpoint(int index) override { }
	void SetModelMatrix(const VSMatrix& matrix, const VSMatrix& normalMatrix) override { objectToWorld = matrix; normalToWorld = normalMatrix; }
	void SetTextureMatrix(const VSMatrix& matrix) override { mTextureMatrix = matrix; }
	int UploadLights(const FDynLightData& lightdata) override { return -1; }
	int UploadBones(const TArray<VSMatrix>& bones) override { return -1; }
	int UploadFogballs(const TArray<Fogball>& balls) override { return -1; }

	// Draw commands
	void DoDraw(int dt, int index, int count, bool apply = true) override;
	void DoDrawIndexed(int dt, int index, int count, bool apply = true) override;

	// Immediate render state change commands. These only change infrequently and should not clutter the render state.
	void SetDepthFunc(int func) override;

	// Commands not relevant for mesh building
	void Clear(int targets) override { }
	void ClearScreen() override { }
	void SetScissor(int x, int y, int w, int h) override { }
	void SetViewport(int x, int y, int w, int h) override { }
	void EnableLineSmooth(bool on) override { }
	void EnableDrawBuffers(int count, bool apply) override { }
	void SetDepthRange(float min, float max) override { }
	bool SetDepthClamp(bool on) override { return false; }
	void SetDepthMask(bool on) override { }
	void SetColorMask(bool r, bool g, bool b, bool a) override { }
	void SetStencil(int offs, int op, int flags = -1) override { }
	void SetCulling(int mode) override { }
	void EnableStencil(bool on) override { }
	void EnableDepthTest(bool on) override { }

	struct DrawLists
	{
		TArray<MeshDrawCommand> mDraws;
		TArray<MeshDrawCommand> mIndexedDraws;
	};
	std::map<MeshApplyState, DrawLists> mSortedLists;

	TArray<FFlatVertex> mVertices;
	TArray<uint32_t> mIndexes;

	VSMatrix objectToWorld = VSMatrix::identity();
	VSMatrix normalToWorld = VSMatrix::identity();

private:
	void Apply();

	int mDepthFunc = 0;
	VSMatrix mTextureMatrix = VSMatrix::identity();
	DrawLists* mDrawLists = nullptr;
};

class MeshBuilderModelVertexBuffer : public IModelVertexBuffer
{
public:
	FModelVertex* LockVertexBuffer(unsigned int size) override
	{
		return static_cast<FModelVertex*>(vbuf.Lock(size));
	}

	void UnlockVertexBuffer() override
	{
	}

	unsigned int* LockIndexBuffer(unsigned int size) override
	{
		return static_cast<unsigned int*>(ibuf.Lock(size));
	}

	void UnlockIndexBuffer() override
	{
	}

	MeshBuilderBuffer<FModelVertex> vbuf;
	MeshBuilderBuffer<unsigned int> ibuf;
};

class MeshBuilderModelRender : public FModelRenderer
{
public:
	MeshBuilderModelRender(MeshBuilder& renderstate);

	ModelRendererType GetType() const override { return ModelRendererType::MeshBuilderRendererType; }

	void BeginDrawModel(FRenderStyle style, int smf_flags, const VSMatrix& objectToWorldMatrix, bool mirrored) override;
	void EndDrawModel(FRenderStyle style, int smf_flags) override;

	IModelVertexBuffer* CreateVertexBuffer(bool needindex, bool singleframe) override;

	VSMatrix GetViewToWorldMatrix() override;

	void BeginDrawHUDModel(FRenderStyle style, const VSMatrix& objectToWorldMatrix, bool mirrored, int smf_flags) override;
	void EndDrawHUDModel(FRenderStyle style, int smf_flags) override;

	void SetInterpolation(double interpolation) override;
	void SetMaterial(FGameTexture* skin, bool clampNoFilter, FTranslationID translation, void* act) override;
	void DrawArrays(int start, int count) override;
	void DrawElements(int numIndices, size_t offset) override;
	void SetupFrame(FModel* model, unsigned int frame1, unsigned int frame2, unsigned int size, int boneStartIndex) override;
	int UploadBones(const TArray<VSMatrix>& bones) override;

private:
	MeshBuilder& renderstate;
};
