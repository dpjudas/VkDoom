
void main()
{
	//
	// calculate fog factor
	//
	
	#uifdef(FOG_RADIAL)
		float fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
	#uelse
		float fogdist = max(16.0, pixelpos.w);
	#uendif
	float fogfactor = exp2 (uFogDensity * fogdist);
	
	FragColor = vec4(uFogColor.rgb, 1.0 - fogfactor);
	
#uifdef(GBUFFER_PASS)
{
	FragFog = vec4(0.0, 0.0, 0.0, 1.0);
	FragNormal = vec4(0.5, 0.5, 0.5, 1.0);
}
#uendif
}

