
layout(set = 0, binding = 0) uniform sampler2D ShadowMap;
layout(set = 0, binding = 1) uniform sampler2DArray LightMap;
layout(set = 0, binding = 2) uniform sampler2D LinearDepth;
layout(set = 0, binding = 3) uniform samplerCubeArray IrradianceMap;
layout(set = 0, binding = 4) uniform samplerCubeArray PrefilterMap;

#if defined(USE_RAYTRACE)
#if defined(SUPPORTS_RAYQUERY)
layout(set = 0, binding = 5) uniform accelerationStructureEXT TopLevelAS;
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
layout(std430, set = 0, binding = 5) buffer NodeBuffer
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
layout(std430, set = 0, binding = 6) buffer VertexBuffer { SurfaceVertex vertices[]; };
layout(std430, set = 0, binding = 7) buffer ElementBuffer { int elements[]; };
#endif
#endif
