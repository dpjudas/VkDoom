
void main()
{
	//
	// calculate fog factor
	//
	
	float fogdist;
	if (FOG_RADIAL)
		fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
	else
		fogdist = max(16.0, pixelpos.w);

	float fogfactor = exp2 (uFogDensity * fogdist);
	
	FragColor = vec4(uFogColor.rgb, 1.0 - fogfactor);
	
#if defined(GBUFFER_PASS)
	FragFog = vec4(0.0, 0.0, 0.0, 1.0);
	FragNormal = vec4(0.5, 0.5, 0.5, 1.0);
#endif
}

