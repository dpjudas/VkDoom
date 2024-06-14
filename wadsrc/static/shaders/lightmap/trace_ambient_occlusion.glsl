
#include <shaders/lightmap/montecarlo.glsl>

float TraceAORay(vec3 origin, float tmin, vec3 dir, float tmax);

float TraceAmbientOcclusion(vec3 origin, vec3 normal)
{
	const float minDistance = 0.01;
	const float aoDistance = 100;
	const int SampleCount = 128;

	vec3 N = normal;
	vec3 up = abs(N.x) < abs(N.y) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
	vec3 tangent = normalize(cross(up, N));
	vec3 bitangent = cross(N, tangent);

	float ambience = 0.0f;
	for (uint i = 0; i < SampleCount; i++)
	{
		vec2 Xi = Hammersley(i, SampleCount);
		vec3 H = normalize(vec3(Xi.x * 2.0f - 1.0f, Xi.y * 2.0f - 1.0f, 1.5 - length(Xi)));
		vec3 L = H.x * tangent + H.y * bitangent + H.z * N;
		ambience += clamp(TraceAORay(origin, minDistance, L, aoDistance) / aoDistance, 0.0, 1.0);
	}
	return ambience / float(SampleCount);
}

float TraceAORay(vec3 origin, float tmin, vec3 dir, float tmax)
{
	float tcur = 0.0;
	for (int i = 0; i < 3; i++)
	{
		TraceResult result = TraceFirstHit(origin, tmin, dir, tmax - tcur);
		if (result.primitiveIndex == -1)
			return tmax;

		SurfaceInfo surface = GetSurface(result.primitiveIndex);

		// Stop if hit sky portal
		if (surface.Sky > 0.0)
			return tmax;

		// Stop if opaque surface
		if (surface.PortalIndex == 0 /*surface.TextureIndex == 0*/)
		{
			return tcur + result.t;
		}

		// Move to surface hit point
		origin += dir * result.t;
		tcur += result.t;
		if (tcur >= tmax)
			return tmax;

		// Move through the portal, if any
		TransformRay(surface.PortalIndex, origin, dir);
	}
	return tmax;
}
