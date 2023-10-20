
// This must match the HWViewpointUniforms struct
layout(set = 1, binding = 0, std140) uniform ViewpointUBO
{
	mat4 ProjectionMatrix;
	mat4 ViewMatrix;
	mat4 NormalViewMatrix;

	vec4 uCameraPos;
	vec4 uClipLine;

	float uGlobVis;			// uGlobVis = R_GetGlobVis(r_visibility) / 32.0
	int uPalLightLevels;	
	int uViewHeight;		// Software fuzz scaling
	float uClipHeight;
	float uClipHeightDirection;
	int uShadowmapFilter;
		
	int uLightBlendMode;
};

layout(set = 1, binding = 1, std140) uniform MatricesUBO
{
	mat4 ModelMatrix;
	mat4 NormalModelMatrix;
	mat4 TextureMatrix;
};

// This must match the SurfaceUniforms struct
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
	float padding2;
	float padding3;
};

#ifdef USE_LEVELMESH
layout(set = 1, binding = 2, std430) buffer SurfaceUniformsSSO
{
	SurfaceUniforms data[];
};
#else
layout(set = 1, binding = 2, std140) uniform SurfaceUniformsUBO
{
	SurfaceUniforms data[MAX_SURFACE_UNIFORMS];
};
#endif

// light buffers
layout(set = 1, binding = 3, std140) uniform LightBufferUBO
{
	vec4 lights[MAX_LIGHT_DATA];
};

struct Fogball
{
	vec3 position;
	float radius;
	vec3 color;
	float fog;
};

layout(set = 1, binding = 4, std140) uniform FogballBufferUBO
{
	Fogball fogballs[MAX_FOGBALL_DATA];
};

// bone matrix buffers
layout(set = 1, binding = 5, std430) buffer BoneBufferSSO
{
	mat4 bones[];
};
