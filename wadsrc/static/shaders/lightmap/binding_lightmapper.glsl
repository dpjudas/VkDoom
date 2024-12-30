#include <shaders/lightmap/binding_struct_definitions.glsl>

layout(set = 0, binding = 0) uniform readonly Uniforms
{
	vec3 SunDir;
	float Padding1;
	vec3 SunColor;
	float SunIntensity;
};

layout(set = 0, binding = 1) buffer readonly SurfaceIndexBuffer { uint surfaceIndices[]; };
layout(set = 0, binding = 2) buffer readonly SurfaceBuffer { SurfaceInfo surfaces[]; };
layout(set = 0, binding = 3) buffer readonly LightBuffer { LightInfo lights[]; };
layout(set = 0, binding = 4) buffer readonly LightIndexBuffer { int lightIndexes[]; };
layout(set = 0, binding = 5) buffer readonly PortalBuffer { PortalInfo portals[]; };

layout(std430, set = 0, binding = 6) buffer readonly ConstantsBuffer { LightmapRaytracePC constants[]; };
