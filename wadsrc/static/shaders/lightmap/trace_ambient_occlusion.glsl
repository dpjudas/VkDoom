
vec2 Hammersley(uint i, uint N);
float RadicalInverse_VdC(uint bits);

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

vec2 Hammersley(uint i, uint N)
{
	return vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

float RadicalInverse_VdC(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10f; // / 0x100000000
}
