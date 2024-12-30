#include <shaders/lightmap/binding_struct_definitions.glsl>

#if defined(USE_RAYQUERY)

layout(set = 1, binding = 0) uniform accelerationStructureEXT acc;

#else

layout(std430, set = 1, binding = 0) buffer readonly NodeBuffer
{
	int nodesRoot;
	int nodebufferPadding1;
	int nodebufferPadding2;
	int nodebufferPadding3;
	CollisionNode nodes[];
};

#endif

layout(std430, set = 1, binding = 1) buffer readonly VertexBuffer { SurfaceVertex vertices[]; };
layout(std430, set = 1, binding = 2) buffer readonly ElementBuffer { int elements[]; };
