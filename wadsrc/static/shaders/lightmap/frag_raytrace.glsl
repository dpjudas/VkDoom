
#include <shaders/lightmap/binding_lightmapper.glsl>
#include <shaders/lightmap/binding_raytrace.glsl>
#include <shaders/lightmap/binding_textures.glsl>
#include <shaders/lightmap/polyfill_rayquery.glsl>
#include <shaders/lightmap/trace_levelmesh.glsl>
#include <shaders/lightmap/trace_sunlight.glsl>
#include <shaders/lightmap/trace_light.glsl>
#include <shaders/lightmap/trace_ambient_occlusion.glsl>

layout(location = 0) centroid in vec3 worldpos;
layout(location = 1) in flat int InstanceIndex;

layout(location = 0) out vec4 fragcolor;

void main()
{
	uint LightStart = constants[InstanceIndex].LightStart;
	uint LightEnd = constants[InstanceIndex].LightEnd;
	int SurfaceIndex = constants[InstanceIndex].SurfaceIndex;

	vec3 normal = surfaces[SurfaceIndex].Normal;
	vec3 origin = worldpos + normal * 0.01;

#if defined(USE_SUNLIGHT)
	vec3 incoming = TraceSunLight(origin, normal, SurfaceIndex);
#else
	vec3 incoming = vec3(0.0);
#endif

	for (uint j = LightStart; j < LightEnd; j++)
	{
		incoming += TraceLight(origin, normal, lights[j], SurfaceIndex);
	}

#if defined(USE_AO)
	incoming.rgb *= TraceAmbientOcclusion(origin, normal);
#endif

	fragcolor = vec4(incoming, 1.0);
}
