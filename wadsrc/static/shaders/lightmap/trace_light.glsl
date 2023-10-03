
vec2 getVogelDiskSample(int sampleIndex, int sampleCount, float phi);

vec3 TraceLight(vec3 origin, vec3 normal, LightInfo light, int surfaceIndex)
{
	const float minDistance = 0.01;
	vec3 incoming = vec3(0.0);
	float dist = distance(light.RelativeOrigin, origin);
	if (dist > minDistance && dist < light.Radius)
	{
		vec3 dir = normalize(light.RelativeOrigin - origin);

		float distAttenuation = max(1.0 - (dist / light.Radius), 0.0);
		float angleAttenuation = 1.0f;
		if (surfaceIndex >= 0)
		{
			angleAttenuation = max(dot(normal, dir), 0.0);
		}
		float spotAttenuation = 1.0;
		if (light.OuterAngleCos > -1.0)
		{
			float cosDir = dot(dir, light.SpotDir);
			spotAttenuation = smoothstep(light.OuterAngleCos, light.InnerAngleCos, cosDir);
			spotAttenuation = max(spotAttenuation, 0.0);
		}

		float attenuation = distAttenuation * angleAttenuation * spotAttenuation;
		if (attenuation > 0.0)
		{
#if defined(USE_SOFTSHADOWS)

			vec3 v = (abs(dir.x) > abs(dir.y)) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
			vec3 xdir = normalize(cross(dir, v));
			vec3 ydir = cross(dir, xdir);

			float lightsize = 10;
			int step_count = 10;
			for (int i = 0; i < step_count; i++)
			{
				vec2 gridoffset = getVogelDiskSample(i, step_count, gl_FragCoord.x + gl_FragCoord.y * 13.37) * lightsize;
				vec3 pos = light.Origin + xdir * gridoffset.x + ydir * gridoffset.y;

				rayColor = vec4(light.Color.rgb, 1.0);
				if (TracePoint(origin, pos, minDistance, normalize(pos - origin), distance(origin, pos)))
				{
					incoming.rgb += (rayColor.rgb * rayColor.w) * (attenuation * light.Intensity) / float(step_count);
				}
			}
			
#else
			rayColor = vec4(light.Color.rgb, 1.0);
			if(TracePoint(origin, light.Origin, minDistance, dir, dist))
			{
				incoming.rgb += (rayColor.rgb * rayColor.w) * (attenuation * light.Intensity);
			}
#endif
		}
	}

	return incoming;
}

vec2 getVogelDiskSample(int sampleIndex, int sampleCount, float phi) 
{
    const float goldenAngle = radians(180.0) * (3.0 - sqrt(5.0));
    float sampleIndexF = float(sampleIndex);
    float sampleCountF = float(sampleCount);
    
    float r = sqrt((sampleIndexF + 0.5) / sampleCountF);  // Assuming index and count are positive
    float theta = sampleIndexF * goldenAngle + phi;
    
    float sine = sin(theta);
    float cosine = cos(theta);
    
    return vec2(cosine, sine) * r;
}
