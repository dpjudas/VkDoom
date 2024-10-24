
#include <shaders/lightmap/binding_viewer.glsl>
#include <shaders/lightmap/polyfill_rayquery.glsl>
#include <shaders/lightmap/trace_levelmesh.glsl>
#include <shaders/lightmap/trace_sunlight.glsl>
#include <shaders/lightmap/trace_light.glsl>
#include <shaders/lightmap/trace_ambient_occlusion.glsl>

layout(location = 0) in vec3 FragRay;
layout(location = 0) out vec4 fragcolor;

void main()
{
	vec3 incoming = vec3(0.1);

	vec3 origin = CameraPos;
	vec3 L = normalize(FragRay);
	TraceResult result = TraceFirstHit(origin, 0.0, L, 10000.0);
	if (result.primitiveIndex != -1)
	{
		SurfaceInfo surface = GetSurface(result.primitiveIndex);
		vec3 surfacepos = origin + L * result.t;

		if (surface.Sky == 0.0)
		{
			incoming += TraceSunLight(surfacepos, surface.Normal);

			uint LightStart = surface.LightStart;
			uint LightEnd = surface.LightEnd;
			for (uint j = LightStart; j < LightEnd; j++)
			{
				incoming += TraceLight(surfacepos, surface.Normal, lights[lightIndexes[j]], 0.0, false);
			}

			// incoming *= TraceAmbientOcclusion(surfacepos, surface.Normal);
		}
		else
		{
			incoming = SunColor;
		}

		if (surface.TextureIndex != 0)
		{
			vec2 uv = GetSurfaceUV(result.primitiveIndex, result.primitiveWeights);
			vec4 color = texture(textures[surface.TextureIndex], uv);
			incoming *= color.rgb;
		}
		else
		{
			incoming = vec3(0.0, 0.0, 1.0);
		}
	}
	else
	{
		incoming = vec3(1.0, 0.0, 0.0);
	}

	fragcolor = vec4(incoming, 1.0);
}
