#pragma once

#include "zstring.h"
#include "tarray.h"

#include "hw_useruniforms.h"

struct PostProcessShader
{
	FString Target;
	FString ShaderLumpName;
	int ShaderVersion = 0;

	FString Name;
	bool Enabled = false;

	//TMap<FString, UserUniformValue> Uniforms;
	UserUniforms Uniforms;
	TMap<FString, FString> Textures;
};

extern TArray<PostProcessShader> PostProcessShaders;

