
// This must match the HWViewpointUniforms struct
layout(set = 1, binding = 0, std140) uniform readonly ViewpointUBO
{
	mat4 ProjectionMatrix;
	mat4 ViewMatrix;
	mat4 NormalViewMatrix;

	vec4 uCameraPos;
	vec4 uClipLine;

	ivec2 uViewOffset;

	float uGlobVis;			// uGlobVis = R_GetGlobVis(r_visibility) / 32.0
	int uPalLightLevels;	
	int uViewHeight;		// Software fuzz scaling
	float uClipHeight;
	float uClipHeightDirection;
	int uShadowmapFilter;

	int uLightTilesWidth;	// Levelmesh light tiles
};

layout(set = 1, binding = 1, std140) uniform readonly MatricesUBO
{
	mat4 ModelMatrix;
	mat4 NormalModelMatrix;
	mat4 TextureMatrix;
};

// This must match the C++ SurfaceUniforms struct
struct SurfaceUniforms
{
	vec4 uObjectColor;
	vec4 uObjectColor2;
	vec4 uDynLightColor;
	vec4 uAddColor;
	vec4 uTextureAddColor;
	vec4 uTextureModulateColor;
	vec4 uTextureBlendColor;
	vec4 uFogColor;
	float uDesaturationFactor;
	float uInterpolationFactor;
	float timer; // timer data for material shaders
	int useVertexData;
	vec4 uVertexColor;
	vec4 uVertexNormal;

	vec4 uGlowTopPlane;
	vec4 uGlowTopColor;
	vec4 uGlowBottomPlane;
	vec4 uGlowBottomColor;

	vec4 uGradientTopPlane;
	vec4 uGradientBottomPlane;

	vec4 uSplitTopPlane;
	vec4 uSplitBottomPlane;

	vec4 uDetailParms;
	vec4 uNpotEmulation;

	vec2 uClipSplit;
	vec2 uSpecularMaterial;

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
	vec4 uVertexColor;
	float uDesaturationFactor;
	float uLightLevel;
	uint padding0;
	uint padding1;
};

struct Fogball
{
	vec3 position;
	float radius;
	vec3 color;
	float fog;
};

#ifdef USE_LEVELMESH

layout(set = 1, binding = 2, std430) buffer readonly SurfaceUniformsSSO
{
	SurfaceUniforms data[];
};

layout(set = 1, binding = 3, std430) buffer readonly SurfaceLightUniformsSSO
{
	SurfaceLightUniforms lightdata[];
};

layout(set = 1, binding = 4, std430) buffer readonly LightBufferSSO
{
	vec4 lights[];
};

layout(set = 1, binding = 5, std140) uniform readonly FogballBufferUBO
{
	Fogball fogballs[MAX_FOGBALL_DATA];
};

// bone matrix buffers
layout(set = 1, binding = 6, std430) buffer readonly BoneBufferSSO
{
	mat4 bones[];
};

#else

layout(set = 1, binding = 2, std140) uniform readonly SurfaceUniformsUBO
{
	SurfaceUniforms data[MAX_SURFACE_UNIFORMS];
};

// light buffers
layout(set = 1, binding = 3, std140) uniform readonly LightBufferUBO
{
	vec4 lights[MAX_LIGHT_DATA];
};

layout(set = 1, binding = 4, std140) uniform readonly FogballBufferUBO
{
	Fogball fogballs[MAX_FOGBALL_DATA];
};

// bone matrix buffers
layout(set = 1, binding = 5, std430) buffer readonly BoneBufferSSO
{
	mat4 bones[];
};

#endif
