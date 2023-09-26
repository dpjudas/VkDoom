
vec2 Hammersley(uint i, uint N);
float RadicalInverse_VdC(uint bits);

float TraceAmbientOcclusion(vec3 origin, vec3 normal)
{
	const float minDistance = 0.05;
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

		float hitDistance;
		int primitiveID = TraceFirstHitTriangleT(origin, minDistance, L, aoDistance, hitDistance);
		if (primitiveID != -1)
		{
			SurfaceInfo surface = surfaces[surfaceIndices[primitiveID]];
			if (surface.Sky == 0.0)
			{
				ambience += clamp(hitDistance / aoDistance, 0.0, 1.0);
			}
		}
		else
		{
			ambience += 1.0;
		}
	}
	return ambience / float(SampleCount);
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
