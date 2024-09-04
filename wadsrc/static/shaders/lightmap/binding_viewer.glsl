
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

layout(set = 0, binding = 0, std430) buffer SurfaceIndexBuffer { uint surfaceIndices[]; };
layout(set = 0, binding = 1, std430) buffer SurfaceBuffer { SurfaceInfo surfaces[]; };
layout(set = 0, binding = 2, std430) buffer LightBuffer { LightInfo lights[]; };
layout(set = 0, binding = 3, std430) buffer LightIndexBuffer { int lightIndexes[]; };
layout(set = 0, binding = 4, std430) buffer PortalBuffer { PortalInfo portals[]; };

#if defined(USE_RAYQUERY)

layout(set = 0, binding = 5) uniform accelerationStructureEXT acc;

#else

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

layout(set = 0, binding = 5, std430) buffer NodeBuffer
{
	int nodesRoot;
	int nodebufferPadding1;
	int nodebufferPadding2;
	int nodebufferPadding3;
	CollisionNode nodes[];
};

#endif

struct SurfaceVertex // Note: this must always match the FFlatVertex struct
{
	vec3 pos;
	float lindex;
	vec2 uv;
	vec2 luv;
};

layout(set = 0, binding = 6, std430) buffer VertexBuffer { SurfaceVertex vertices[]; };
layout(set = 0, binding = 7, std430) buffer ElementBuffer { int elements[]; };

layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(push_constant) uniform PushConstants
{
	mat4 ViewToWorld;
	vec3 CameraPos;
	float ProjX;
	vec3 SunDir;
	float ProjY;
	vec3 SunColor;
	float SunIntensity;
};
