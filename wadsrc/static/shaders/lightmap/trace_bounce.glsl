
#include <shaders/lightmap/montecarlo.glsl>

vec3 TraceBounceLight(vec3 origin, vec3 normal)
{
	const float minDistance = 0.01;
	const float maxDistance = 1000.0;
	const int SampleCount = 8;

	vec3 N = normal;
	vec3 up = abs(N.x) < abs(N.y) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
	vec3 tangent = normalize(cross(up, N));
	vec3 bitangent = cross(N, tangent);
	vec3 incoming = vec3(0.0);

	for (uint i = 0; i < SampleCount; i++)
	{
		vec2 Xi = Hammersley(i, SampleCount);
		vec3 H = normalize(vec3(Xi.x * 2.0f - 1.0f, Xi.y * 2.0f - 1.0f, 1.5 - length(Xi)));
		vec3 L = H.x * tangent + H.y * bitangent + H.z * N;

		TraceResult result = TraceFirstHit(origin, minDistance, L, maxDistance);

		// We hit nothing.
		if (result.primitiveIndex == -1)
			continue;

		SurfaceInfo surface = GetSurface(result.primitiveIndex);
		uint LightStart = surface.LightStart;
		uint LightEnd = surface.LightEnd;
		vec3 surfacepos = origin + L * result.t;

		float angleAttenuation = max(dot(normal, L), 0.0);

#if defined(USE_SUNLIGHT)
		incoming += TraceSunLight(surfacepos, surface.Normal) * angleAttenuation;
#endif

		for (uint j = LightStart; j < LightEnd; j++)
		{
			incoming += TraceLight(surfacepos, surface.Normal, lights[lightIndexes[j]], result.t) * angleAttenuation;
		}
	}
	return incoming / float(SampleCount);
}
