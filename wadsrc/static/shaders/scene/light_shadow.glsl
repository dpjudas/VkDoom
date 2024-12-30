
// Check if light is in shadow

#if defined(USE_RAYTRACE)

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

float traceShadow(vec4 lightpos, int quality, float softShadowRadius)
{
	vec3 origin = pixelpos.xyz + vWorldNormal.xyz;
	vec3 target = lightpos.xyz + 0.01; // nudge light position slightly as Doom maps tend to have their lights perfectly aligned with planes

	vec3 direction = normalize(target - origin);
	float dist = distance(origin, target);

	if (quality == 0 || softShadowRadius == 0)
	{
		return traceHit(origin, direction, dist);
	}
	else
	{
		vec3 v = (abs(direction.x) > abs(direction.y)) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
		vec3 xdir = normalize(cross(direction, v));
		vec3 ydir = cross(direction, xdir);

		float sum = 0.0;
		int step_count = quality * 4;
		for (int i = 0; i < step_count; i++)
		{
			vec2 gridoffset = getVogelDiskSample(i, step_count, gl_FragCoord.x + gl_FragCoord.y * 13.37) * softShadowRadius;
			vec3 pos = target + xdir * gridoffset.x + ydir * gridoffset.y;
			sum += traceHit(origin, normalize(pos - origin), dist);
		}
		return (sum / step_count);
	}
}

float shadowAttenuation(vec4 lightpos, float lightcolorA, float softShadowRadius)
{
	if (lightpos.w > 1000000.0)
		return 1.0; // Sunlight
	return traceShadow(lightpos, uShadowmapFilter, softShadowRadius);
}

#elif defined(USE_SHADOWMAP)

float shadowDirToU(vec2 dir)
{
	if (abs(dir.y) > abs(dir.x))
	{
		float x = dir.x / dir.y * 0.125;
		if (dir.y >= 0.0)
			return 0.125 + x;
		else
			return (0.50 + 0.125) + x;
	}
	else
	{
		float y = dir.y / dir.x * 0.125;
		if (dir.x >= 0.0)
			return (0.25 + 0.125) - y;
		else
			return (0.75 + 0.125) - y;
	}
}

vec2 shadowUToDir(float u)
{
	u *= 4.0;
	vec2 raydir;
	switch (int(u))
	{
	case 0: raydir = vec2(u * 2.0 - 1.0, 1.0); break;
	case 1: raydir = vec2(1.0, 1.0 - (u - 1.0) * 2.0); break;
	case 2: raydir = vec2(1.0 - (u - 2.0) * 2.0, -1.0); break;
	case 3: raydir = vec2(-1.0, (u - 3.0) * 2.0 - 1.0); break;
	}
	return raydir;
}

float sampleShadowmap(vec3 planePoint, float v)
{
	float bias = 1.0;
	float negD = dot(vWorldNormal.xyz, planePoint);

	vec3 ray = planePoint;

	vec2 isize = textureSize(ShadowMap, 0);
	float scale = float(isize.x) * 0.25;

	// Snap to shadow map texel grid
	if (abs(ray.z) > abs(ray.x))
	{
		ray.y = ray.y / abs(ray.z);
		ray.x = ray.x / abs(ray.z);
		ray.x = (floor((ray.x + 1.0) * 0.5 * scale) + 0.5) / scale * 2.0 - 1.0;
		ray.z = sign(ray.z);
	}
	else
	{
		ray.y = ray.y / abs(ray.x);
		ray.z = ray.z / abs(ray.x);
		ray.z = (floor((ray.z + 1.0) * 0.5 * scale) + 0.5) / scale * 2.0 - 1.0;
		ray.x = sign(ray.x);
	}

	float t = negD / dot(vWorldNormal.xyz, ray) - bias;
	vec2 dir = ray.xz * t;

	float u = shadowDirToU(dir);
	float dist2 = dot(dir, dir);
	return step(dist2, texture(ShadowMap, vec2(u, v)).x);
}

float sampleShadowmapPCF(vec3 planePoint, float v)
{
	float bias = 1.0;
	float negD = dot(vWorldNormal.xyz, planePoint);

	vec3 ray = planePoint;

	if (abs(ray.z) > abs(ray.x))
		ray.y = ray.y / abs(ray.z);
	else
		ray.y = ray.y / abs(ray.x);

	vec2 isize = textureSize(ShadowMap, 0);
	float scale = float(isize.x);
	float texelPos = floor(shadowDirToU(ray.xz) * scale);

	float sum = 0.0;
	float step_count = uShadowmapFilter;

	texelPos -= step_count + 0.5;
	for (float x = -step_count; x <= step_count; x++)
	{
		float u = fract(texelPos / scale);
		vec2 dir = shadowUToDir(u);

		ray.x = dir.x;
		ray.z = dir.y;
		float t = negD / dot(vWorldNormal.xyz, ray) - bias;
		dir = ray.xz * t;

		float dist2 = dot(dir, dir);
		sum += step(dist2, texture(ShadowMap, vec2(u, v)).x);
		texelPos++;
	}
	return sum / (uShadowmapFilter * 2.0 + 1.0);
}

float shadowmapAttenuation(vec4 lightpos, float shadowIndex)
{
	vec3 planePoint = pixelpos.xyz - lightpos.xyz;
	planePoint += 0.01; // nudge light position slightly as Doom maps tend to have their lights perfectly aligned with planes

	if (dot(planePoint.xz, planePoint.xz) < 1.0)
		return 1.0; // Light is too close

	float v = (shadowIndex + 0.5) / 1024.0;

	if (uShadowmapFilter == 0)
	{
		return sampleShadowmap(planePoint, v);
	}
	else
	{
		return sampleShadowmapPCF(planePoint, v);
	}
}

float shadowAttenuation(vec4 lightpos, float lightcolorA, float softShadowRadius)
{
	if (lightpos.w > 1000000.0)
		return 1.0; // Sunlight

	float shadowIndex = abs(lightcolorA) - 1.0;
	if (shadowIndex >= 1024.0)
		return 1.0; // No shadowmap available for this light

	return shadowmapAttenuation(lightpos, shadowIndex);
}

#else

float shadowAttenuation(vec4 lightpos, float lightcolorA, float softShadowRadius)
{
	return 1.0;
}

#endif
