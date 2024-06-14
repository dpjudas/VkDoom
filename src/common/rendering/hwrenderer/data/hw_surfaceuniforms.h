
#pragma once

#include "vectors.h"

struct SurfaceUniforms
{
	FVector4 uObjectColor;
	FVector4 uObjectColor2;
	FVector4 uDynLightColor;
	FVector4 uAddColor;
	FVector4 uTextureAddColor;
	FVector4 uTextureModulateColor;
	FVector4 uTextureBlendColor;
	FVector4 uFogColor;
	float uDesaturationFactor; // HWDrawInfo::SetColor
	float uInterpolationFactor;
	float timer;
	int useVertexData;
	FVector4 uVertexColor; // HWDrawInfo::SetColor
	FVector4 uVertexNormal;

	FVector4 uGlowTopPlane;
	FVector4 uGlowTopColor;
	FVector4 uGlowBottomPlane;
	FVector4 uGlowBottomColor;

	FVector4 uGradientTopPlane;
	FVector4 uGradientBottomPlane;

	FVector4 uSplitTopPlane;
	FVector4 uSplitBottomPlane;

	FVector4 uDetailParms;
	FVector4 uNpotEmulation;

	FVector2 uClipSplit;
	FVector2 uSpecularMaterial;

	float uLightLevel; // HWDrawInfo::SetColor
	float uFogDensity;
	float uLightFactor;
	float uLightDist;

	float uAlphaThreshold;
	int uTextureIndex;
	float padding2;
	float padding3;
};
