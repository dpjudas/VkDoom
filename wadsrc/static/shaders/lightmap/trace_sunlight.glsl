
vec3 TraceSunLight(vec3 origin)
{
	const float minDistance = 0.01;
	vec3 incoming = vec3(0.0);
	const float dist = 32768.0;

	rayColor = vec4(SunColor.rgb * SunIntensity, 1.0);

	int primitiveID = TraceFirstHitTriangle(origin, minDistance, SunDir, dist);
	if (primitiveID != -1)
	{
		SurfaceInfo surface = surfaces[surfaceIndices[primitiveID]];
		incoming.rgb = rayColor.rgb * rayColor.w * surface.Sky;
	}
	return incoming;
}
