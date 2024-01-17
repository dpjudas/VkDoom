
layout(set = 0, binding = 0) uniform sampler2D ShadowMap;
layout(set = 0, binding = 1) uniform sampler2DArray LightMap;

#if defined(USE_RAYTRACE)
#if defined(SUPPORTS_RAYQUERY)
layout(set = 0, binding = 2) uniform accelerationStructureEXT TopLevelAS;
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
layout(std430, set = 0, binding = 2) buffer NodeBuffer
{
	int nodesRoot;
	int nodebufferPadding1;
	int nodebufferPadding2;
	int nodebufferPadding3;
	CollisionNode nodes[];
};
struct SurfaceVertex // Note: this must always match the FFlatVertex struct
{
	vec3 pos;
	float lindex;
	vec2 uv;
	vec2 luv;
};
layout(std430, set = 0, binding = 3) buffer VertexBuffer { SurfaceVertex vertices[]; };
layout(std430, set = 0, binding = 4) buffer ElementBuffer { int elements[]; };
#endif
#endif
