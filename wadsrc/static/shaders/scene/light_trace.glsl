
#include <shaders/lightmap/polyfill_rayquery.glsl>
#include <shaders/lightmap/trace_levelmesh.glsl>
#include <shaders/lightmap/montecarlo.glsl>

bool TraceHitIsFacing(vec3 hitPos, SurfaceInfo hitSurface)
{
	vec3 target = uCameraPos.xyz - hitPos;
	return dot(hitSurface.Normal, target) < 0;
}

float TraceDynLightRay(vec3 origin, float tmin, vec3 direction, float dist)
{
	float alpha = 1.0;

	for (int i = 0; i < 3; i++)
	{
		TraceResult result;
		SurfaceInfo surface;
		if (PRECISE_MIDTEXTURES)
		{
			bool skip = true;

			{
				TraceResult frontResult = TraceFirstHit(origin, tmin, direction, dist);
				TraceResult backResult = TraceFirstHitReverse(origin, tmin, direction, dist);
				
				if(frontResult.primitiveIndex != -1 && backResult.primitiveIndex != -1)
				{
					//both hit
					SurfaceInfo frontSurface = GetSurface(frontResult.primitiveIndex);
					SurfaceInfo backSurface = GetSurface(backResult.primitiveIndex);
					
					bool frontFacing = TraceHitIsFacing(origin + direction * frontResult.t, frontSurface);
					bool backFacing = TraceHitIsFacing(origin + direction * backResult.t, backSurface);

					if(frontFacing && frontFacing == backFacing)
					{
						skip = false;
						if(frontResult.t < backResult.t)
						{
							result = frontResult;
							surface = frontSurface;
						}
						else
						{
							result = backResult;
							surface = backSurface;
						}
					}
					else if(backFacing)
					{
						skip = false;
						result = backResult;
						surface = backSurface;
					}
					else
					{
						skip = !frontFacing;
						result = frontResult;
						surface = frontSurface;
					}
				}
				else if(frontResult.primitiveIndex != -1)
				{
					result = frontResult;
					surface = GetSurface(frontResult.primitiveIndex);
					skip = TraceHitIsFacing(origin + direction * result.t, surface);
				}
				else if(backResult.primitiveIndex != -1)
				{
					result = backResult;
					surface = GetSurface(backResult.primitiveIndex);
					skip = TraceHitIsFacing(origin + direction * result.t, surface);
				}
				else
				{
					// neither hit
					return alpha;
				}
			}

			if(!skip)
			{
				alpha = PassRayThroughSurfaceDynLight(surface, GetSurfaceUV(result.primitiveIndex, result.primitiveWeights), alpha);
				
				// Stop if there is no light left
				if (alpha <= 0.0)
					return 0.0;
			}
		}
		else
		{
			result = TraceFirstHit(origin, tmin, direction, dist);

			// Stop if we hit nothing - the point light is visible.
			if (result.primitiveIndex == -1)
				return alpha;

			surface = GetSurface(result.primitiveIndex);
		
			// Pass through surface texture
			alpha = PassRayThroughSurfaceDynLight(surface, GetSurfaceUV(result.primitiveIndex, result.primitiveWeights), alpha);
			
			// Stop if there is no light left
			if (alpha <= 0.0)
				return 0.0;
		}

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
	if (USE_RAYTRACE_PRECISE)
		return TraceDynLightRay(origin, 0.01f, direction, dist);
	else
		return TraceAnyHit(origin, 0.01f, direction, dist) ? 0.0 : 1.0;
}

float traceShadow(vec3 lightpos, float softShadowRadius)
{
	vec3 target = lightpos.xyz + 0.01; // nudge light position slightly as Doom maps tend to have their lights perfectly aligned with planes
	vec3 origin;
	vec3 direction;
	if (USE_SPRITE_CENTER)
	{
		origin = uActorCenter.xyz;
		direction = normalize(target - origin);
	}
	else if (LIGHT_NONORMALS)
	{
		origin = pixelpos.xyz;
		direction = normalize(target - origin);
		origin -= direction;
	}
	else
	{
		origin = pixelpos.xyz + (vWorldNormal.xyz * 0.1);
		direction = normalize(target - origin);
	}

	float dist = distance(origin, target);

#if defined(SHADE_VERTEX)
	return traceHit(origin, direction, dist);
#else
	if(softShadowRadius == 0)
	{
		return traceHit(origin, direction, dist);
	}
	else
	{
		vec3 v = (abs(direction.x) > abs(direction.y)) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
		vec3 xdir = normalize(cross(direction, v));
		vec3 ydir = cross(direction, xdir);

		if (SHADOWMAP_FILTER > 0)
		{
			float sum = 0.0;
			const int step_count = SHADOWMAP_FILTER * 4;

			if (USE_RAYTRACE_PRECISE)
			{
				for (int i = 0; i < step_count; i++)
				{
					vec2 gridoffset = getVogelDiskSample(i, step_count, gl_FragCoord.x + gl_FragCoord.y * 13.37) * softShadowRadius;
					vec3 pos = target + xdir * gridoffset.x + ydir * gridoffset.y;
					sum += TraceDynLightRay(origin, 0.01f, normalize(pos - origin), dist);
				}
			}
			else
			{
				for (int i = 0; i < step_count; i++)
				{
					vec2 gridoffset = getVogelDiskSample(i, step_count, gl_FragCoord.x + gl_FragCoord.y * 13.37) * softShadowRadius;
					vec3 pos = target + xdir * gridoffset.x + ydir * gridoffset.y;
					sum += TraceAnyHit(origin, 0.01f, normalize(pos - origin), dist) ? 0.0 : 1.0;
				}
			}

			return (sum / step_count);
		}
		else
		{
			const int samples_per_dimension = 4;
			const int sample_count = samples_per_dimension * samples_per_dimension;
			const int index = (int(int(gl_FragCoord.x) % samples_per_dimension + gl_FragCoord.y * samples_per_dimension)) % sample_count;

			vec2 gridoffset = getVogelDiskSample(index, sample_count, 0) * softShadowRadius;
			vec3 pos = target + xdir * gridoffset.x + ydir * gridoffset.y;
			return traceHit(origin, normalize(pos - origin), dist);
		}
	}
#endif
}

float traceSun(vec3 SunDir)
{
	vec3 origin;
	if (USE_SPRITE_CENTER)
	{
		 origin = uActorCenter.xyz;
	}
	else if (LIGHT_NONORMALS)
	{
		origin = pixelpos.xyz;
		origin -= SunDir;
	}
	else
	{
		origin = pixelpos.xyz + (vWorldNormal.xyz * 0.1);
	}

	float dist = 65536.0;

#if defined(SHADE_VERTEX)
	return TraceDynLightRay(origin, 0.01f, SunDir, dist);
#else
	if (SHADOWMAP_FILTER == 0)
	{
		return TraceDynLightRay(origin, 0.01f, SunDir, dist);
	}
	else
	{
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
	}
#endif
}
