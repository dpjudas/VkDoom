
#pragma once

#include "textures.h"

class FMaterial;

struct FMaterialState
{
	FMaterial* mMaterial = nullptr;
	int mClampMode = 0;
	int mTranslation = CLAMP_NONE;
	int mOverrideShader = -1;
	bool mChanged = false;
	bool mPaletteMode = false;
	GlobalShaderAddr globalShaderAddr = {0, 3, 0}; // null global shader entry, TODO

	void Reset()
	{
		mMaterial = nullptr;
		mTranslation = 0;
		mClampMode = CLAMP_NONE;
		mOverrideShader = -1;
		mChanged = false;
		mPaletteMode = false;
		globalShaderAddr = {0, 3, 0};
	}
};
