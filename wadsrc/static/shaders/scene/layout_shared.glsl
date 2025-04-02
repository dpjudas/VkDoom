
#include "shaders/scene/binding_fixed.glsl"
#include "shaders/scene/binding_rsbuffers.glsl"
#include "shaders/scene/binding_textures.glsl"

// material types
#if defined(SPECULAR)
	#define normaltexture texture2
	#define speculartexture texture3
	#define brighttexture texture4
	#define detailtexture texture5
	#define glowtexture texture6
#elif defined(PBR)
	#define normaltexture texture2
	#define metallictexture texture3
	#define roughnesstexture texture4
	#define aotexture texture5
	#define brighttexture texture6
	#define detailtexture texture7
	#define glowtexture texture8
#else
	#define brighttexture texture2
	#define detailtexture texture3
	#define glowtexture texture4
#endif

#define BrdfLUT 1 // the BRDF convoluted texture is always in this texture slot

#define uObjectColor data[uDataIndex].uObjectColor
#define uObjectColor2 data[uDataIndex].uObjectColor2
#define uDynLightColor data[uDataIndex].uDynLightColor
#define uAddColor data[uDataIndex].uAddColor
#define uTextureBlendColor data[uDataIndex].uTextureBlendColor
#define uTextureModulateColor data[uDataIndex].uTextureModulateColor
#define uTextureAddColor data[uDataIndex].uTextureAddColor
#define uFogColor data[uDataIndex].uFogColor
#define uInterpolationFactor data[uDataIndex].uInterpolationFactor
#define timer data[uDataIndex].timer
#define useVertexData data[uDataIndex].useVertexData
#define uVertexNormal data[uDataIndex].uVertexNormal
#define uGlowTopPlane data[uDataIndex].uGlowTopPlane
#define uGlowTopColor data[uDataIndex].uGlowTopColor
#define uGlowBottomPlane data[uDataIndex].uGlowBottomPlane
#define uGlowBottomColor data[uDataIndex].uGlowBottomColor
#define uGradientTopPlane data[uDataIndex].uGradientTopPlane
#define uGradientBottomPlane data[uDataIndex].uGradientBottomPlane
#define uSplitTopPlane data[uDataIndex].uSplitTopPlane
#define uSplitBottomPlane data[uDataIndex].uSplitBottomPlane
#define uDetailParms data[uDataIndex].uDetailParms
#define uNpotEmulation data[uDataIndex].uNpotEmulation
#define uClipSplit data[uDataIndex].uClipSplit
#define uSpecularMaterial data[uDataIndex].uSpecularMaterial
#define uFogDensity data[uDataIndex].uFogDensity
#define uLightFactor data[uDataIndex].uLightFactor
#define uLightDist data[uDataIndex].uLightDist
#define uAlphaThreshold data[uDataIndex].uAlphaThreshold
#define uTextureIndex data[uDataIndex].uTextureIndex
#define uDepthFadeThreshold data[uDataIndex].uDepthFadeThreshold
#define uActorCenter data[uDataIndex].uActorCenter
#define uLightProbeIndex data[uDataIndex].uLightProbeIndex

#if defined(USE_LEVELMESH)
	#define uVertexColor lightdata[uDataIndex].uVertexColor
	#define uDesaturationFactor lightdata[uDataIndex].uDesaturationFactor
	#define uLightLevel lightdata[uDataIndex].uLightLevel
	int uLightIndex;
#else
	#define uVertexColor data[uDataIndex].uVertexColor
	#define uDesaturationFactor data[uDataIndex].uDesaturationFactor
	#define uLightLevel data[uDataIndex].uLightLevel
#endif

#define VULKAN_COORDINATE_SYSTEM
#define HAS_UNIFORM_VERTEX_DATA

// GLSL spec 4.60, 8.15. Noise Functions
// https://www.khronos.org/registry/OpenGL/specs/gl/GLSLangSpec.4.60.pdf
//  "The noise functions noise1, noise2, noise3, and noise4 have been deprecated starting with version 4.4 of GLSL.
//   When not generating SPIR-V they are defined to return the value 0.0 or a vector whose components are all 0.0.
//   When generating SPIR-V the noise functions are not declared and may not be used."
// However, we need to support mods with custom shaders created for OpenGL renderer
float noise1(float) { return 0; }
vec2 noise2(vec2) { return vec2(0); }
vec3 noise3(vec3) { return vec3(0); }
vec4 noise4(vec4) { return vec4(0); }

vec4 texture(int index, vec2 p)
{
#ifndef FRAGSHADER
	return textureLod(textures[uTextureIndex + index], p, 0.0);
#else
	return texture(textures[nonuniformEXT(uTextureIndex + index)], p);
#endif
}

vec4 texture(int index, vec2 p, float bias)
{
#ifndef FRAGSHADER
	return textureLod(textures[uTextureIndex + index], p, 0.0);
#else
	return texture(textures[nonuniformEXT(uTextureIndex + index)], p, bias);
#endif
}

ivec2 textureSize(int index, int lod)
{
	return textureSize(textures[nonuniformEXT(uTextureIndex + index)], lod);
}

vec4 textureGrad(int index, vec2 P, vec2 dPdx, vec2 dPdy)
{
	return textureGrad(textures[nonuniformEXT(uTextureIndex + index)], P, dPdx, dPdy);
}

vec4 textureLod(int index, vec2 P, float lod)
{
	return textureLod(textures[nonuniformEXT(uTextureIndex + index)], P, lod);
}

vec4 texelFetch(int index, ivec2 P, int lod)
{
	return texelFetch(textures[nonuniformEXT(uTextureIndex + index)], P, lod);
}

vec4 texelFetchSampler2D(sampler2D s, ivec2 P, int lod)
{
	return texelFetch(s, P, lod);
}
