#pragma once

#include "vectors.h"
#include "matrix.h"
#include "hw_material.h"
#include "hw_levelmesh.h"
#include "texmanip.h"
#include "version.h"
#include "i_interface.h"
#include "hw_viewpointuniforms.h"
#include "hw_cvars.h"

#include <atomic>

struct FColormap;
class IBuffer;
struct HWViewpointUniforms;
struct FDynLightData;

enum EClearTarget
{
	CT_Depth = 1,
	CT_Stencil = 2,
	CT_Color = 4
};

enum ERenderEffect
{
	EFF_NONE = -1,
	EFF_FOGBOUNDARY,
	EFF_SPHEREMAP,
	EFF_BURN,
	EFF_STENCIL,
	EFF_PORTAL,

	MAX_EFFECTS
};

enum EAlphaFunc
{
	Alpha_GEqual = 0,
	Alpha_Greater = 1
};

enum EDrawType
{
	DT_Points = 0,
	DT_Lines = 1,
	DT_Triangles = 2,
	DT_TriangleFan = 3,
	DT_TriangleStrip = 4
};

enum EDepthFunc
{
	DF_Less,
	DF_LEqual,
	DF_Always
};

enum EStencilFlags
{
	SF_AllOn = 0,
	SF_ColorMaskOff = 1,
	SF_DepthMaskOff = 2,
};

enum EStencilOp
{
	SOP_Keep = 0,
	SOP_Increment = 1,
	SOP_Decrement = 2
};

enum ECull
{
	Cull_None,
	Cull_CCW,
	Cull_CW
};



struct FStateVec4
{
	float vec[4];

	void Set(float r, float g, float b, float a)
	{
		vec[0] = r;
		vec[1] = g;
		vec[2] = b;
		vec[3] = a;
	}
};

struct FDepthBiasState
{
	float mFactor;
	float mUnits;
	bool mChanged;

	void Reset()
	{
		mFactor = 0;
		mUnits = 0;
		mChanged = false;
	}
};

struct FFlatVertex;

enum EPassType
{
	NORMAL_PASS,
	GBUFFER_PASS,
	MAX_PASS_TYPES
};

inline FVector4 toFVector4(PalEntry pe) { const float normScale = 1.0f / 255.0f; return FVector4(pe.r * normScale, pe.g * normScale, pe.b * normScale, pe.a * normScale); }

struct Fogball
{
	FVector3 Position;
	float Radius;
	FVector3 Color;
	float Fog;
};

class FRenderState
{
protected:
	uint8_t mFogEnabled;
	uint8_t mTextureEnabled:1;
	uint8_t mGlowEnabled : 1;
	uint8_t mGradientEnabled : 1;
	uint8_t mSplitEnabled : 1;
	uint8_t mBrightmapEnabled : 1;

	int mLightIndex;
	int mBoneIndexBase;
	int mFogballIndex;
	int mSpecialEffect;
	int mTextureMode;
	int mTextureClamp;
	int mTextureModeFlags;
	int mSoftLight;
	int mLightMode = -1;

	int mColorMapSpecial;
	float mColorMapFlash;

	SurfaceUniforms mSurfaceUniforms = {};
	PalEntry mFogColor;

	FRenderStyle mRenderStyle;

	FMaterialState mMaterial;
	FDepthBiasState mBias;

	IBuffer* mVertexBuffer;
	int mVertexOffsets[2];	// one per binding point
	IBuffer* mIndexBuffer;

	EPassType mPassType = NORMAL_PASS;

public:

	uint64_t firstFrame = 0;

	void Reset()
	{
		mTextureEnabled = true;
		mBrightmapEnabled = mGradientEnabled = mFogEnabled = mGlowEnabled = false;
		mFogColor = 0xffffffff;
		mSurfaceUniforms.uFogColor = toFVector4(mFogColor);
		mTextureMode = -1;
		mTextureClamp = 0;
		mTextureModeFlags = 0;
		mSurfaceUniforms.uDesaturationFactor = 0.0f;
		mSurfaceUniforms.uAlphaThreshold = 0.5f;
		mSplitEnabled = false;
		mSurfaceUniforms.uAddColor = toFVector4(PalEntry(0));
		mSurfaceUniforms.uObjectColor = toFVector4(PalEntry(0xffffffff));
		mSurfaceUniforms.uObjectColor2 = toFVector4(PalEntry(0));
		mSurfaceUniforms.uTextureBlendColor = toFVector4(PalEntry(0));
		mSurfaceUniforms.uTextureAddColor = toFVector4(PalEntry(0));
		mSurfaceUniforms.uTextureModulateColor = toFVector4(PalEntry(0));
		mSoftLight = 0;
		mSurfaceUniforms.uLightDist = 0.0f;
		mSurfaceUniforms.uLightFactor = 0.0f;
		mSurfaceUniforms.uFogDensity = 0.0f;
		mSurfaceUniforms.uLightLevel = -1.0f;
		mSpecialEffect = EFF_NONE;
		mLightIndex = -1;
		mBoneIndexBase = -1;
		mFogballIndex = -1;
		mSurfaceUniforms.uInterpolationFactor = 0;
		mRenderStyle = DefaultRenderStyle();
		mMaterial.Reset();
		mBias.Reset();
		mPassType = NORMAL_PASS;

		mColorMapSpecial = 0;
		mColorMapFlash = 1;

		mVertexBuffer = nullptr;
		mVertexOffsets[0] = mVertexOffsets[1] = 0;
		mIndexBuffer = nullptr;

		mSurfaceUniforms.uVertexColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		mSurfaceUniforms.uGlowTopColor = { 0.0f, 0.0f, 0.0f, 0.0f };
		mSurfaceUniforms.uGlowBottomColor = { 0.0f, 0.0f, 0.0f, 0.0f };
		mSurfaceUniforms.uGlowTopPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
		mSurfaceUniforms.uGlowBottomPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
		mSurfaceUniforms.uGradientTopPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
		mSurfaceUniforms.uGradientBottomPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
		mSurfaceUniforms.uSplitTopPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
		mSurfaceUniforms.uSplitBottomPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
		mSurfaceUniforms.uDynLightColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		mSurfaceUniforms.uDetailParms = { 0.0f, 0.0f, 0.0f, 0.0f };
#ifdef NPOT_EMULATION
		mSurfaceUniforms.uNpotEmulation = { 0,0,0,0 };
#endif
		ClearClipSplit();
	}

	void SetNormal(FVector3 norm)
	{
		mSurfaceUniforms.uVertexNormal = { norm.X, norm.Y, norm.Z, 0.f };
	}

	void SetNormal(float x, float y, float z)
	{
		mSurfaceUniforms.uVertexNormal = { x, y, z, 0.f };
	}

	void SetColor(float r, float g, float b, float a = 1.f, int desat = 0)
	{
		mSurfaceUniforms.uVertexColor = { r, g, b, a };
		mSurfaceUniforms.uDesaturationFactor = desat * (1.0f / 255.0f);
	}

	void SetColor(PalEntry pe, int desat = 0)
	{
		const float scale = 1.0f / 255.0f;
		mSurfaceUniforms.uVertexColor = { pe.r * scale, pe.g * scale, pe.b * scale, pe.a * scale };
		mSurfaceUniforms.uDesaturationFactor = desat * (1.0f / 255.0f);
	}

	void SetColorAlpha(PalEntry pe, float alpha = 1.f, int desat = 0)
	{
		const float scale = 1.0f / 255.0f;
		mSurfaceUniforms.uVertexColor = { pe.r * scale, pe.g * scale, pe.b * scale, alpha };
		mSurfaceUniforms.uDesaturationFactor = desat * (1.0f / 255.0f);
	}

	void ResetColor()
	{
		mSurfaceUniforms.uVertexColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		mSurfaceUniforms.uDesaturationFactor = 0.0f;
	}

	void SetTextureClamp(bool on)
	{
		if (on) mTextureClamp = TM_CLAMPY;
		else mTextureClamp = 0;
	}

	void SetTextureMode(int mode)
	{
		mTextureMode = mode;
	}

	void SetTextureMode(FRenderStyle style)
	{
		if (style.Flags & STYLEF_RedIsAlpha)
		{
			SetTextureMode(TM_ALPHATEXTURE);
		}
		else if (style.Flags & STYLEF_ColorIsFixed)
		{
			SetTextureMode(TM_STENCIL);
		}
		else if (style.Flags & STYLEF_InvertSource)
		{
			SetTextureMode(TM_INVERSE);
		}
	}

	int GetTextureMode()
	{
		return mTextureMode;
	}

	int GetTextureModeAndFlags(int tempTM)
	{
		int f = mTextureModeFlags;
		if (!mBrightmapEnabled) f &= ~(TEXF_Brightmap | TEXF_Glowmap);
		if (mTextureClamp) f |= TEXF_ClampY;
		return (mTextureMode == TM_NORMAL && tempTM == TM_OPAQUE ? TM_OPAQUE : mTextureMode) | f;
	}

	void EnableTexture(bool on)
	{
		mTextureEnabled = on;
	}

	void EnableFog(uint8_t on)
	{
		mFogEnabled = on;
	}

	void SetEffect(int eff)
	{
		mSpecialEffect = eff;
	}

	void EnableGlow(bool on)
	{
		if (mGlowEnabled && !on)
		{
			mSurfaceUniforms.uGlowTopColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			mSurfaceUniforms.uGlowBottomColor = { 0.0f, 0.0f, 0.0f, 0.0f };
		}
		mGlowEnabled = on;
	}

	void EnableGradient(bool on)
	{
		mGradientEnabled = on;
	}

	void EnableBrightmap(bool on)
	{
		mBrightmapEnabled = on;
	}

	void EnableSplit(bool on)
	{
		if (mSplitEnabled && !on)
		{
			mSurfaceUniforms.uSplitTopPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
			mSurfaceUniforms.uSplitBottomPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
		}
		mSplitEnabled = on;
	}

	void SetGlowParams(float *t, float *b)
	{
		mSurfaceUniforms.uGlowTopColor = { t[0], t[1], t[2], t[3] };
		mSurfaceUniforms.uGlowBottomColor = { b[0], b[1], b[2], b[3] };
	}

	void SetSoftLightLevel(int llevel, int blendfactor = 0)
	{
		if (blendfactor == 0) mSurfaceUniforms.uLightLevel = llevel / 255.f;
		else mSurfaceUniforms.uLightLevel = -1.f;
	}

	void SetNoSoftLightLevel()
	{
		mSurfaceUniforms.uLightLevel = -1.f;
	}

	void SetLightMode(int lightmode)
	{
		mLightMode = lightmode;
	}

	void SetGlowPlanes(const FVector4 &tp, const FVector4& bp)
	{
		mSurfaceUniforms.uGlowTopPlane = tp;
		mSurfaceUniforms.uGlowBottomPlane = bp;
	}

	void SetGradientPlanes(const FVector4& tp, const FVector4& bp)
	{
		mSurfaceUniforms.uGradientTopPlane = tp;
		mSurfaceUniforms.uGradientBottomPlane = bp;
	}

	void SetSplitPlanes(const FVector4& tp, const FVector4& bp)
	{
		mSurfaceUniforms.uSplitTopPlane = tp;
		mSurfaceUniforms.uSplitBottomPlane = bp;
	}

	void SetDetailParms(float xscale, float yscale, float bias)
	{
		mSurfaceUniforms.uDetailParms = { xscale, yscale, bias, 0 };
	}

	void SetDynLight(float r, float g, float b)
	{
		mSurfaceUniforms.uDynLightColor.X = r;
		mSurfaceUniforms.uDynLightColor.Y = g;
		mSurfaceUniforms.uDynLightColor.Z = b;
	}

	void SetScreenFade(float f)
	{
		// This component is otherwise unused.
		mSurfaceUniforms.uDynLightColor.W = f;
	}

	void SetObjectColor(PalEntry pe)
	{
		mSurfaceUniforms.uObjectColor = toFVector4(pe);
	}

	void SetObjectColor2(PalEntry pe)
	{
		mSurfaceUniforms.uObjectColor2 = toFVector4(pe);
	}

	void SetAddColor(PalEntry pe)
	{
		mSurfaceUniforms.uAddColor = toFVector4(pe);
	}

	void SetNpotEmulation(float factor, float offset)
	{
#ifdef NPOT_EMULATION
		mSurfaceUniforms.uNpotEmulation = { offset, factor, 0, 0 };
#endif
	}

	void ApplyTextureManipulation(TextureManipulation* texfx)
	{
		if (!texfx || texfx->AddColor.a == 0)
		{
			mSurfaceUniforms.uTextureAddColor.W = 0.0f;	// we only need to set the flags to 0
		}
		else
		{
			// set up the whole thing
			const float normScale = 1.0f / 255.0f;
			mSurfaceUniforms.uTextureAddColor = FVector4(texfx->AddColor.r * normScale, texfx->AddColor.g * normScale, texfx->AddColor.b * normScale, texfx->AddColor.a);
			auto pe = texfx->ModulateColor;
			mSurfaceUniforms.uTextureModulateColor = FVector4(pe.r * pe.a / 255.f, pe.g * pe.a / 255.f, pe.b * pe.a / 255.f, texfx->DesaturationFactor);
			mSurfaceUniforms.uTextureBlendColor = toFVector4(texfx->BlendColor);
		}
	}
	void SetTextureColors(float* modColor, float* addColor, float* blendColor)
	{
		mSurfaceUniforms.uTextureAddColor = FVector4(addColor[0], addColor[1], addColor[2], addColor[3]);
		mSurfaceUniforms.uTextureModulateColor = FVector4(modColor[0], modColor[1], modColor[2], modColor[3]);
		mSurfaceUniforms.uTextureBlendColor = FVector4(blendColor[0], blendColor[1], blendColor[2], blendColor[3]);
	}

	void SetFog(PalEntry c, float d)
	{
		const float LOG2E = 1.442692f;	// = 1/log(2)
		mFogColor = c;
		mSurfaceUniforms.uFogColor = toFVector4(mFogColor);
		if (d >= 0.0f) mSurfaceUniforms.uFogDensity = d * (-LOG2E / 64000.f);
	}

	void SetLightParms(float f, float d)
	{
		mSurfaceUniforms.uLightFactor = f;
		mSurfaceUniforms.uLightDist = d;
	}

	PalEntry GetFogColor() const
	{
		return mFogColor;
	}

	void AlphaFunc(int func, float thresh)
	{
		if (func == Alpha_Greater) mSurfaceUniforms.uAlphaThreshold = thresh;
		else mSurfaceUniforms.uAlphaThreshold = thresh - 0.001f;
	}

	void SetLightIndex(int index)
	{
		mLightIndex = index;
	}

	void SetBoneIndexBase(int index)
	{
		mBoneIndexBase = index;
	}

	void SetFogballIndex(int index)
	{
		mFogballIndex = index;
	}

	void SetRenderStyle(FRenderStyle rs)
	{
		mRenderStyle = rs;
	}

	void SetRenderStyle(ERenderStyle rs)
	{
		mRenderStyle = rs;
	}

	auto GetDepthBias()
	{
		return mBias;
	}

	void SetDepthBias(float a, float b)
	{
		mBias.mChanged |= mBias.mFactor != a || mBias.mUnits != b;
		mBias.mFactor = a;
		mBias.mUnits = b;
	}

	void SetDepthBias(FDepthBiasState& bias)
	{
		SetDepthBias(bias.mFactor, bias.mUnits);
	}

	void ClearDepthBias()
	{
		mBias.mChanged |= mBias.mFactor != 0 || mBias.mUnits != 0;
		mBias.mFactor = 0;
		mBias.mUnits = 0;
	}

private:
	void SetMaterial(FMaterial *mat, int clampmode, int translation, int overrideshader)
	{
		mMaterial.mMaterial = mat;
		mMaterial.mClampMode = clampmode;
		mMaterial.mTranslation = translation;
		mMaterial.mOverrideShader = overrideshader;
		mMaterial.mChanged = true;
		mTextureModeFlags = mat->GetLayerFlags();
		auto scale = mat->GetDetailScale();
		mSurfaceUniforms.uDetailParms = { scale.X, scale.Y, 2, 0 };
	}

public:
	void SetMaterial(FGameTexture* tex, EUpscaleFlags upscalemask, int scaleflags, int clampmode, int translation, int overrideshader)
	{
		tex->setSeen();
		if (!sysCallbacks.PreBindTexture || !sysCallbacks.PreBindTexture(this, tex, upscalemask, scaleflags, clampmode, translation, overrideshader))
		{
			if (shouldUpscale(tex, upscalemask)) scaleflags |= CTF_Upscale;
		}
		auto mat = FMaterial::ValidateTexture(tex, scaleflags);
		assert(mat);
		SetMaterial(mat, clampmode, translation, overrideshader);
	}

	void SetMaterial(FGameTexture* tex, EUpscaleFlags upscalemask, int scaleflags, int clampmode, FTranslationID translation, int overrideshader)
	{
		SetMaterial(tex, upscalemask, scaleflags, clampmode, translation.index(), overrideshader);
	}


	void SetClipSplit(float bottom, float top)
	{
		mSurfaceUniforms.uClipSplit.X = bottom;
		mSurfaceUniforms.uClipSplit.Y = top;
	}

	void SetClipSplit(float *vals)
	{
		mSurfaceUniforms.uClipSplit.X = vals[0];
		mSurfaceUniforms.uClipSplit.Y = vals[1];
	}

	void GetClipSplit(float *out)
	{
		out[0] = mSurfaceUniforms.uClipSplit.X;
		out[1] = mSurfaceUniforms.uClipSplit.Y;
	}

	void ClearClipSplit()
	{
		mSurfaceUniforms.uClipSplit.X = -1000000.f;
		mSurfaceUniforms.uClipSplit.Y = 1000000.f;
	}

	void SetVertexBuffer(IBuffer* vb, int offset0 = 0, int offset1 = 0)
	{
		mVertexBuffer = vb;
		mVertexOffsets[0] = offset0;
		mVertexOffsets[1] = offset1;
	}

	void SetIndexBuffer(IBuffer* ib)
	{
		mIndexBuffer = ib;
	}

	void SetFlatVertexBuffer()
	{
		SetVertexBuffer(nullptr);
		SetIndexBuffer(nullptr);
	}

	void SetInterpolationFactor(float fac)
	{
		mSurfaceUniforms.uInterpolationFactor = fac;
	}

	float GetInterpolationFactor()
	{
		return mSurfaceUniforms.uInterpolationFactor;
	}

	void EnableDrawBufferAttachments(bool on) // Used by fog boundary drawer
	{
		EnableDrawBuffers(on ? GetPassDrawBufferCount() : 1);
	}

	int GetPassDrawBufferCount()
	{
		return mPassType == GBUFFER_PASS ? 3 : 1;
	}

	void SetPassType(EPassType passType)
	{
		mPassType = passType;
	}

	EPassType GetPassType()
	{
		return mPassType;
	}

	void SetSpecialColormap(int cm, float flash)
	{
		mColorMapSpecial = cm;
		mColorMapFlash = flash;
	}

	int Set2DViewpoint(int width, int height, int palLightLevels = 0)
	{
		HWViewpointUniforms matrices;
		matrices.mViewMatrix.loadIdentity();
		matrices.mNormalViewMatrix.loadIdentity();
		matrices.mViewHeight = 0;
		matrices.mGlobVis = 1.f;
		matrices.mPalLightLevels = palLightLevels;
		matrices.mClipLine.X = -10000000.0f;
		matrices.mShadowFilter = gl_light_shadow_filter;
		matrices.mLightBlendMode = 0;
		matrices.mProjectionMatrix.ortho(0, (float)width, (float)height, 0, -1.0f, 1.0f);
		matrices.CalcDependencies();
		return SetViewpoint(matrices);
	}

	// API-dependent render interface

	// Worker threads
	virtual void FlushCommands() { }

	// Vertices
	virtual std::pair<FFlatVertex*, unsigned int> AllocVertices(unsigned int count) = 0;
	virtual void SetShadowData(const TArray<FFlatVertex>& vertices, const TArray<uint32_t>& indexes) = 0;
	virtual void UpdateShadowData(unsigned int index, const FFlatVertex* vertices, unsigned int count) = 0;
	virtual void ResetVertices() = 0;

	// Buffers
	virtual int SetViewpoint(const HWViewpointUniforms& vp) = 0;
	virtual void SetViewpoint(int index) = 0;
	virtual void SetModelMatrix(const VSMatrix& matrix, const VSMatrix& normalMatrix) = 0;
	virtual void SetTextureMatrix(const VSMatrix& matrix) = 0;
	virtual int UploadLights(const FDynLightData& lightdata) = 0;
	virtual int UploadBones(const TArray<VSMatrix>& bones) = 0;
	virtual int UploadFogballs(const TArray<Fogball>& balls) = 0;

	// Draw commands
	virtual void ClearScreen() = 0;
	virtual void Draw(int dt, int index, int count, bool apply = true) = 0;
	virtual void DrawIndexed(int dt, int index, int count, bool apply = true) = 0;

	// Immediate render state change commands. These only change infrequently and should not clutter the render state.
	virtual bool SetDepthClamp(bool on) = 0;					// Deactivated only by skyboxes.
	virtual void SetDepthMask(bool on) = 0;						// Used by decals and indirectly by portal setup.
	virtual void SetDepthFunc(int func) = 0;					// Used by models, portals and mirror surfaces.
	virtual void SetDepthRange(float min, float max) = 0;		// Used by portal setup.
	virtual void SetColorMask(bool r, bool g, bool b, bool a) = 0;	// Used by portals.
	virtual void SetStencil(int offs, int op, int flags=-1) = 0;	// Used by portal setup and render hacks.
	virtual void SetCulling(int mode) = 0;						// Used by model drawer only.
	virtual void Clear(int targets) = 0;						// not used during normal rendering
	virtual void EnableStencil(bool on) = 0;					// always on for 3D, always off for 2D
	virtual void SetScissor(int x, int y, int w, int h) = 0;	// constant for 3D, changes for 2D
	virtual void SetViewport(int x, int y, int w, int h) = 0;	// constant for all 3D and all 2D
	virtual void EnableDepthTest(bool on) = 0;					// used by 2D, portals and render hacks.
	virtual void EnableLineSmooth(bool on) = 0;					// constant setting for each 2D drawer operation
	virtual void EnableDrawBuffers(int count, bool apply = false) = 0;	// Used by SSAO and EnableDrawBufferAttachments

	void SetColorMask(bool on)
	{
		SetColorMask(on, on, on, on);
	}

	// Draw level mesh
	virtual void DrawLevelMeshSurfaces(bool noFragmentShader) { }
	virtual void DrawLevelMeshPortals(bool noFragmentShader) { }
	virtual int GetNextQueryIndex() { return 0; }
	virtual void BeginQuery() { }
	virtual void EndQuery() { }
	virtual void GetQueryResults(int start, int count, TArray<bool>& results) { }

	friend class Mesh;
};

