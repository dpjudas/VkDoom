
vec2 getVogelDiskSample(int sampleIndex, int sampleCount, float phi);

vec3 TraceSunLight(vec3 origin, vec3 normal, int surfaceIndex)
{
	float angleAttenuation = 1.0f;
	if (surfaceIndex >= 0)
	{
		angleAttenuation = max(dot(normal, SunDir), 0.0);
		if (angleAttenuation == 0.0)
			return vec3(0.0);
	}

	const float minDistance = 0.01;
	vec3 incoming = vec3(0.0);
	const float dist = 32768.0;

#if defined(USE_SOFTSHADOWS)

	vec3 target = origin + SunDir * dist;
	vec3 dir = SunDir;
	vec3 v = (abs(dir.x) > abs(dir.y)) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 xdir = normalize(cross(dir, v));
	vec3 ydir = cross(dir, xdir);

	float lightsize = 100;
	int step_count = 10;
	for (int i = 0; i < step_count; i++)
	{
		vec2 gridoffset = getVogelDiskSample(i, step_count, gl_FragCoord.x + gl_FragCoord.y * 13.37) * lightsize;
		vec3 pos = target + xdir * gridoffset.x + ydir * gridoffset.y;

		rayColor = vec4(SunColor.rgb * SunIntensity, 1.0);

		int primitiveID = TraceFirstHitTriangle(origin, minDistance, normalize(pos - origin), dist);
		if (primitiveID != -1)
		{
			SurfaceInfo surface = surfaces[surfaceIndices[primitiveID]];
			incoming.rgb += rayColor.rgb * rayColor.w * surface.Sky / float(step_count);
		}
	}
			
#else

	rayColor = vec4(SunColor.rgb * SunIntensity, 1.0);

	int primitiveID = TraceFirstHitTriangle(origin, minDistance, SunDir, dist);
	if (primitiveID != -1)
	{
		SurfaceInfo surface = surfaces[surfaceIndices[primitiveID]];
		incoming.rgb = rayColor.rgb * rayColor.w * surface.Sky;
	}

#endif

	return incoming * angleAttenuation;
}
