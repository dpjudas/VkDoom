
void main()
{
#ifdef USE_LEVELMESH
	FragColor.rgb = texture(tex, vTexCoord.st).rgb * (uLightLevel / 255.0);
	FragColor.a = 1.0;
	if (vLightmap.z >= 0.0)
	{
		FragColor.rgb *= texture(LightMap, vLightmap).rgb;
	}
#else

#ifdef NO_CLIPDISTANCE_SUPPORT
	if (ClipDistanceA.x < 0 || ClipDistanceA.y < 0 || ClipDistanceA.z < 0 || ClipDistanceA.w < 0 || ClipDistanceB.x < 0) discard;
#endif

	Material material = CreateMaterial();

#ifndef NO_ALPHATEST
	if (material.Base.a <= uAlphaThreshold) discard;
#endif

	FragColor = ProcessLightMode(material);

#ifdef GBUFFER_PASS
	FragFog = vec4(AmbientOcclusionColor(), 1.0);
	FragNormal = vec4(vEyeNormal.xyz * 0.5 + 0.5, 1.0);
#endif

#endif
}
