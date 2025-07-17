
vec3 ProcessMaterialLight(Material material, vec3 color, float sunlightAttenuation)
{
	vec4 dynlight = uDynLightColor;
	if (sunlightAttenuation > 0.0)
	{
		sunlightAttenuation *= clamp(dot(material.Normal, SunDir), 0.0, 1.0);
		dynlight.rgb += SunColor.rgb * SunIntensity * sunlightAttenuation;
	}

	return material.Base.rgb * clamp(color + desaturate(dynlight).rgb, 0.0, 1.4);
}

vec3 ProcessSWLight(Material material)
{
	return vec3(0.0);
}
