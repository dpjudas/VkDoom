
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
			rayColor = vec4(light.Color.rgb, 1.0);
			if(TracePoint(origin, light.Origin, minDistance, dir, dist))
			{
				incoming.rgb += (rayColor.rgb * rayColor.w) * (attenuation * light.Intensity);
			}
		}
	}

	return incoming;
}
