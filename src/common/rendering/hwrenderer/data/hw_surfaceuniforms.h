
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
	float uDesaturationFactor;
	float uInterpolationFactor;
	float timer;
	int useVertexData;
	FVector4 uVertexColor;
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

	float uLightLevel;
	float uFogDensity;
	float uLightFactor;
	float uLightDist;

	float uAlphaThreshold;
	int uTextureIndex;
	float uDepthFadeThreshold;
	float padding3;
};

struct SurfaceLightUniforms
{
	FVector4 uVertexColor;
	float uDesaturationFactor;
	float uLightLevel;
	int padding0;
	int padding1;

	void SetColor(float r, float g, float b, float a = 1.f, int desat = 0)
	{
		uVertexColor = { r, g, b, a };
		uDesaturationFactor = desat * (1.0f / 255.0f);
	}

	void SetColor(PalEntry pe, int desat = 0)
	{
		const float scale = 1.0f / 255.0f;
		uVertexColor = { pe.r * scale, pe.g * scale, pe.b * scale, pe.a * scale };
		uDesaturationFactor = desat * (1.0f / 255.0f);
	}

	void SetColorAlpha(PalEntry pe, float alpha = 1.f, int desat = 0)
	{
		const float scale = 1.0f / 255.0f;
		uVertexColor = { pe.r * scale, pe.g * scale, pe.b * scale, alpha };
		uDesaturationFactor = desat * (1.0f / 255.0f);
	}

	void ResetColor()
	{
		uVertexColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		uDesaturationFactor = 0.0f;
	}

	void SetSoftLightLevel(int llevel, int blendfactor = 0)
	{
		if (blendfactor == 0) uLightLevel = llevel / 255.f;
		else uLightLevel = -1.f;
	}

	void SetNoSoftLightLevel()
	{
		uLightLevel = -1.f;
	}
};
