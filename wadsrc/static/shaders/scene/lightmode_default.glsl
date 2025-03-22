
vec4 Lightmode_Default()
{
	vec4 color = vColor;

	if (FOG_BEFORE_LIGHTS)
	{
		// calculate fog factor
		float fogdist;
		if (FOG_RADIAL)
			fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
		else
			fogdist = max(16.0, pixelpos.w);
		float fogfactor = exp2 (uFogDensity * fogdist);

		// brightening around the player for light mode 2
		if (fogdist < uLightDist)
		{
			color.rgb *= uLightFactor - (fogdist / uLightDist) * (uLightFactor - 1.0);
		}

		// apply light diminishing through fog equation
		color.rgb = mix(vec3(0.0, 0.0, 0.0), color.rgb, fogfactor);
	}

	return color;
}
