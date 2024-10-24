#pragma once

#include "matrix.h"

struct HWDrawInfo;

enum class ELightBlendMode : uint8_t
{
	CLAMP = 0,
	CLAMP_COLOR = 1,
	NOCLAMP = 2,

	DEFAULT = CLAMP,
};

enum class ELightAttenuationMode : uint8_t
{
	LINEAR = 0,
	INVERSE_SQUARE = 1,

	DEFAULT = LINEAR,
};

struct HWViewpointUniforms
{
	VSMatrix mProjectionMatrix;
	VSMatrix mViewMatrix;
	VSMatrix mNormalViewMatrix;
	FVector4 mCameraPos;
	FVector4 mClipLine;
	
	int mViewOffsetX = 0;
	int mViewOffsetY = 0;

	float mGlobVis = 1.f;
	int mPalLightLevels = 0;
	int mViewHeight = 0;
	float mClipHeight = 0.f;
	float mClipHeightDirection = 0.f;
	int mShadowFilter = 1;

	int mLightTilesWidth = 0;

	void CalcDependencies()
	{
		mNormalViewMatrix.computeNormalMatrix(mViewMatrix);
	}
};



