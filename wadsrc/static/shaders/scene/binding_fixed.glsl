#include <shaders/lightmap/binding_struct_definitions.glsl>

layout(set = 0, binding = 0) uniform sampler2D ShadowMap;
layout(set = 0, binding = 1) uniform sampler2DArray LightMap;
layout(set = 0, binding = 2) uniform sampler2D LinearDepth;
layout(set = 0, binding = 3) uniform samplerCubeArray IrradianceMap;
layout(set = 0, binding = 4) uniform samplerCubeArray PrefilterMap;

#if defined(USE_RAYTRACE)
#if defined(SUPPORTS_RAYQUERY)

layout(set = 0, binding = 5) uniform accelerationStructureEXT acc;

#else

layout(set = 0, binding = 5, std430) buffer readonly NodeBuffer
{
	int nodesRoot;
	int nodebufferPadding1;
	int nodebufferPadding2;
	int nodebufferPadding3;
	CollisionNode nodes[];
};

#endif

layout(set = 0, binding = 6,  std430) buffer readonly VertexBuffer			{ SurfaceVertex vertices[]; };
layout(set = 0, binding = 7,  std430) buffer readonly ElementBuffer			{ int elements[]; };
layout(set = 0, binding = 8,  std430) buffer readonly SurfaceIndexBuffer	{ uint surfaceIndices[]; };
layout(set = 0, binding = 9,  std430) buffer readonly SurfaceBuffer			{ SurfaceInfo surfaces[]; };
layout(set = 0, binding = 10, std430) buffer readonly PortalBuffer			{ PortalInfo portals[]; };
#endif
