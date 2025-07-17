
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

	int uLightTilesWidth;	// Levelmesh light tiles

	vec3 SunDir;
	float Padding;
	vec3 SunColor;
	float SunIntensity;

	vec3 uCameraNormal;
};

layout(set = 1, binding = 1, std140) uniform readonly MatricesUBO
{
	mat4 ModelMatrix;
	mat4 NormalModelMatrix;
	mat4 TextureMatrix;
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
	LightTileBlock lights[];
};

#define getLightRange() lights[uLightIndex].indices

#define getLights() lights[uLightIndex].lights

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
layout(set = 1, binding = 3, std430) buffer readonly LightBufferSSO
{
	ivec4 lightIndex[MAX_LIGHT_DATA];
	DynLightInfo lights[];
};

#define getLightRange() lightIndex[uLightIndex]

#define getLights() lights

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
