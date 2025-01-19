
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

struct CollisionNode
{
	vec3 center;
	float padding1;
	vec3 extents;
	float padding2;
	int left;
	int right;
	int element_index;
	int padding3;
};

struct SurfaceVertex // Note: this must always match the FFlatVertex struct
{
	vec3 pos;
	float lindex;
	vec2 uv;
	vec2 luv;
};

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
	float padding1;
	vec3 uActorCenter;
	float padding2;
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

#define LIGHTINFO_ATTENUATED 1
#define LIGHTINFO_SHADOWMAPPED 2
#define LIGHTINFO_SPOT 4
#define LIGHTINFO_TRACE 8
#define LIGHTINFO_SUN 16

struct DynLightInfo
{
	vec3 pos; float padding0;
	vec3 color; float padding1;
	vec3 spotDir; float padding2;
	float radius;
	float linarity;
	float softShadowRadius;
	float strength;
	float spotInnerAngle;
	float spotOuterAngle;
	int shadowIndex;
	int flags;
};

struct LightTileBlock
{
	ivec4 indices;
	DynLightInfo lights[16];
};