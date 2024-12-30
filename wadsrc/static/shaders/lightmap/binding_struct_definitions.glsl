
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