
#include <shaders/lightmap/montecarlo.glsl>

vec4 TraceSunRay(vec3 origin, float tmin, vec3 dir, float tmax, vec4 rayColor);

vec3 TraceSunLight(vec3 origin, vec3 normal)
{
	float angleAttenuation = max(dot(normal, SunDir), 0.0);
	if (angleAttenuation == 0.0)
		return vec3(0.0);

	const float minDistance = 0.01;
	vec3 incoming = vec3(0.0);
	const float dist = 65536.0;

	vec4 rayColor = vec4(SunColor.rgb * SunIntensity, 1.0);

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
		incoming.rgb += TraceSunRay(origin, minDistance, normalize(pos - origin), dist, rayColor).rgb / float(step_count);
	}
			
#else

	incoming.rgb = TraceSunRay(origin, minDistance, SunDir, dist, rayColor).rgb;

#endif

	return incoming * angleAttenuation;
}

vec4 TraceSunRay(vec3 origin, float tmin, vec3 dir, float tmax, vec4 rayColor)
{
	for (int i = 0; i < 3; i++)
	{
		TraceResult result = TraceFirstHit(origin, tmin, dir, tmax);

		// We hit nothing. We have to hit a sky surface to hit the sky.
		if (result.primitiveIndex == -1)
			return vec4(0.0);

		SurfaceInfo surface = GetSurface(result.primitiveIndex);

		// Stop if we hit the sky.
		if (surface.Sky > 0.0)
			return rayColor;

		// Blend with surface texture
		rayColor = BlendTexture(surface, GetSurfaceUV(result.primitiveIndex, result.primitiveWeights), rayColor);

		// Stop if there is no light left
		if (rayColor.r + rayColor.g + rayColor.b <= 0.0)
			return vec4(0.0);

		// Move to surface hit point
		origin += dir * result.t;
		tmax -= result.t;
		if (tmax <= tmin)
			return vec4(0.0);

		// Move through the portal, if any
		TransformRay(surface.PortalIndex, origin, dir);
	}
	return vec4(0.0);
}
