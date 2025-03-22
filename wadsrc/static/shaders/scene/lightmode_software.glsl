
float SoftwareColormap(float light, float z)
{
	float L = light * 255.0;
	float vis = min(uGlobVis / z, 24.0 / 32.0);
	float shade = 2.0 - (L + 12.0) / 128.0;
	float lightscale = shade - vis;
	return lightscale * 31.0;
}

vec4 Lightmode_Software()
{
	// z is the depth in view space, positive going into the screen
	float z;
	if (SWLIGHT_RADIAL)
		z = distance(pixelpos.xyz, uCameraPos.xyz);
	else
		z = pixelpos.w;

	float colormap = SoftwareColormap(uLightLevel, z);

	if (SWLIGHT_BANDED)
		colormap = floor(colormap) + 0.5;

	// Result is the normalized colormap index (0 bright .. 1 dark)
	float newlightlevel = 1.0 - clamp(colormap, 0.0, 31.0) / 32.0;

	vec4 color = vColor;
	color.rgb *= newlightlevel;
	return color;
}
