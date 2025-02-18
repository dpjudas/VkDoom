
vec4 Lightmode_Default()
{
	vec4 color = vColor;

	#uifdef(FOG_BEFORE_LIGHTS)
	{
		// calculate fog factor
		float fogdist;
		#uifdef(FOG_RADIAL)
			fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
		#uelse
			fogdist = max(16.0, pixelpos.w);
		#uendif
		float fogfactor = exp2 (uFogDensity * fogdist);

		// brightening around the player for light mode 2
		if (fogdist < uLightDist)
		{
			color.rgb *= uLightFactor - (fogdist / uLightDist) * (uLightFactor - 1.0);
		}

		// apply light diminishing through fog equation
		color.rgb = mix(vec3(0.0, 0.0, 0.0), color.rgb, fogfactor);
	}
	#uendif

	return color;
}
