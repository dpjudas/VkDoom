
layout(set = 0, binding = 0) uniform readonly Uniforms
{
	vec3 SunDir;
	float Padding1;
	vec3 SunColor;
	float SunIntensity;
};

struct SurfaceInfo
{
	vec3 Normal;
	float Sky;
	uint PortalIndex;
	int TextureIndex;
	float Alpha;
	float Padding0;
	uint LightStart;
	uint LightEnd;
	uint Padding1;
	uint Padding2;
};

struct PortalInfo
{
	mat4 Transformation;
};

struct LightInfo
{
	vec3 Origin;
	float Padding0;
	vec3 RelativeOrigin;
	float Padding1;
	float Radius;
	float Intensity;
	float InnerAngleCos;
	float OuterAngleCos;
	vec3 SpotDir;
	float SoftShadowRadius;
	vec3 Color;
	float Padding3;
};

layout(set = 0, binding = 1) buffer readonly SurfaceIndexBuffer { uint surfaceIndices[]; };
layout(set = 0, binding = 2) buffer readonly SurfaceBuffer { SurfaceInfo surfaces[]; };
layout(set = 0, binding = 3) buffer readonly LightBuffer { LightInfo lights[]; };
layout(set = 0, binding = 4) buffer readonly LightIndexBuffer { int lightIndexes[]; };
layout(set = 0, binding = 5) buffer readonly PortalBuffer { PortalInfo portals[]; };

struct LightmapRaytracePC
{
	int SurfaceIndex;
	int Padding0;
	int Padding1;
	int Padding2;
	vec3 WorldToLocal;
	float TextureSize;
	vec3 ProjLocalToU;
	float Padding3;
	vec3 ProjLocalToV;
	float Padding4;
	float TileX;
	float TileY;
	float TileWidth;
	float TileHeight;
};

layout(std430, set = 0, binding = 6) buffer readonly ConstantsBuffer { LightmapRaytracePC constants[]; };
