
#include <shaders/lightmap/polyfill_rayquery.glsl>
#include <shaders/lightmap/trace_levelmesh.glsl>
#include <shaders/lightmap/montecarlo.glsl>

float TraceDynLightRay(vec3 origin, float tmin, vec3 direction, float dist)
{
	float alpha = 1.0;

	for (int i = 0; i < 3; i++)
	{
		TraceResult result = TraceFirstHit(origin, tmin, direction, dist);

		// Stop if we hit nothing - the point light is visible.
		if (result.primitiveIndex == -1)
			return alpha;

		SurfaceInfo surface = GetSurface(result.primitiveIndex);
		
		// Pass through surface texture
		alpha = PassRayThroughSurfaceDynLight(surface, GetSurfaceUV(result.primitiveIndex, result.primitiveWeights), alpha);

		// Stop if there is no light left
		if (alpha <= 0.0)
			return 0.0;

		// Move to surface hit point
		origin += direction * result.t;
		dist -= result.t;

		// Move through the portal, if any
		TransformRay(surface.PortalIndex, origin, direction);
	}

	return 0.0;
}

float traceHit(vec3 origin, vec3 direction, float dist)
{
	#if defined(USE_RAYTRACE_PRECISE)
		return TraceDynLightRay(origin, 0.01f, direction, dist);
	#else
		return TraceAnyHit(origin, 0.01f, direction, dist) ? 0.0 : 1.0;
	#endif
}

float traceShadow(vec3 lightpos, float softShadowRadius)
{
	vec3 target = lightpos.xyz + 0.01; // nudge light position slightly as Doom maps tend to have their lights perfectly aligned with planes
#ifdef USE_SPRITE_CENTER
	vec3 origin = uActorCenter.xyz;
	vec3 direction = normalize(target - origin);
#elif defined(LIGHT_NONORMALS)
	vec3 origin = pixelpos.xyz;
	vec3 direction = normalize(target - origin);
	origin -= direction;
#else
	vec3 origin = pixelpos.xyz + (vWorldNormal.xyz * 0.1);
	vec3 direction = normalize(target - origin);
#endif

	float dist = distance(origin, target);

#if SHADOWMAP_FILTER == 0
	return traceHit(origin, direction, dist);
#else
	if (softShadowRadius == 0)
	{
		return traceHit(origin, direction, dist);
	}
	else
	{
		vec3 v = (abs(direction.x) > abs(direction.y)) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
		vec3 xdir = normalize(cross(direction, v));
		vec3 ydir = cross(direction, xdir);

		float sum = 0.0;
		const int step_count = SHADOWMAP_FILTER * 4;
		for (int i = 0; i < step_count; i++)
		{
			vec2 gridoffset = getVogelDiskSample(i, step_count, gl_FragCoord.x + gl_FragCoord.y * 13.37) * softShadowRadius;
			vec3 pos = target + xdir * gridoffset.x + ydir * gridoffset.y;
			sum += traceHit(origin, normalize(pos - origin), dist);
		}
		return (sum / step_count);
	}
#endif
}

float traceSun(vec3 SunDir)
{
#ifdef USE_SPRITE_CENTER
	vec3 origin = uActorCenter.xyz;
#elif defined(LIGHT_NONORMALS)
	vec3 origin = pixelpos.xyz;
	origin -= SunDir;
#else
	vec3 origin = pixelpos.xyz + (vWorldNormal.xyz * 0.1);
#endif

	float dist = 65536.0;

#if SHADOWMAP_FILTER == 0
	return TraceDynLightRay(origin, 0.01f, SunDir, dist);
#else
	vec3 target = (SunDir * dist) + origin;
	vec3 v = (abs(SunDir.x) > abs(SunDir.y)) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 xdir = normalize(cross(SunDir, v));
	vec3 ydir = cross(SunDir, xdir);

	float sum = 0.0;
	const int step_count = SHADOWMAP_FILTER * 4;
	for (int i = 0; i < step_count; i++)
	{
		vec2 gridoffset = getVogelDiskSample(i, step_count, gl_FragCoord.x + gl_FragCoord.y * 13.37) * 100.0;
		vec3 pos = target + xdir * gridoffset.x + ydir * gridoffset.y;
		sum += TraceDynLightRay(origin, 0.01f, normalize(pos - origin), dist);
	}
	return (sum / step_count);
#endif
}