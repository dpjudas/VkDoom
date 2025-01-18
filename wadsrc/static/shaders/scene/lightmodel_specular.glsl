vec2 lightAttenuation(DynLightInfo light, vec3 normal, vec3 viewdir, float glossiness, float specularLevel)
{
	float lightdistance = distance(light.pos.xyz, pixelpos.xyz);
	if (light.radius < lightdistance)
		return vec2(0.0); // Early out lights touching surface but not this fragment

	float attenuation = distanceAttenuation(lightdistance, light.radius, light.strength, light.linearity);

	if ((light.flags & LIGHTINFO_SPOT) != 0)
	{
		attenuation *= spotLightAttenuation(light.pos.xyz, light.spotDir.xyz, light.spotInnerAngle, light.spotOuterAngle);
	}

	vec3 lightdir = normalize(light.pos.xyz - pixelpos.xyz);

	if ((light.flags & LIGHTINFO_ATTENUATED) != 0)
	{
		attenuation *= clamp(dot(normal, lightdir), 0.0, 1.0);
	}

	if (attenuation > 0.0) // Skip shadow map test if possible
	{
		// light.radius >= 1000000.0 is sunlight(?), skip attenuation
		if(light.radius < 1000000.0 && (light.flags & LIGHTINFO_SHADOWMAPPED) != 0)
		{
			attenuation *= shadowAttenuation(light.pos.xyz, light.shadowIndex, light.softShadowRadius, light.flags);
		}
	}

	if (attenuation <= 0.0)
		return vec2(0.0);

	vec3 halfdir = normalize(viewdir + lightdir);
	float specAngle = clamp(dot(halfdir, normal), 0.0f, 1.0f);
	float phExp = glossiness * 4.0f;
	return vec2(attenuation, attenuation * specularLevel * pow(specAngle, phExp));
}

vec3 ProcessMaterialLight(Material material, vec3 color)
{
	vec4 dynlight = uDynLightColor;
	vec4 specular = vec4(0.0, 0.0, 0.0, 1.0);

	vec3 normal = material.Normal;
	vec3 viewdir = normalize(uCameraPos.xyz - pixelpos.xyz);

	if (uLightIndex >= 0)
	{
		ivec4 lightRange = getLightRange();
		if (lightRange.z > lightRange.x)
		{
			// modulated lights
			for(int i=lightRange.x; i<lightRange.y; i++)
			{
				vec3 lightcolor = getLights()[i].color.rgb;
				vec2 attenuation = lightAttenuation(getLights()[i], normal, viewdir, material.Glossiness, material.SpecularLevel);
				dynlight.rgb += lightcolor.rgb * attenuation.x;
				specular.rgb += lightcolor.rgb * attenuation.y;
			}

			// subtractive lights
			for(int i=lightRange.y; i<lightRange.z; i++)
			{
				vec3 lightcolor = getLights()[i].color.rgb;
				vec2 attenuation = lightAttenuation(getLights()[i], normal, viewdir, material.Glossiness, material.SpecularLevel);
				dynlight.rgb -= lightcolor.rgb * attenuation.x;
				specular.rgb -= lightcolor.rgb * attenuation.y;
			}
		}
	}
	
	#if defined(LIGHT_BLEND_CLAMPED)
		
		dynlight.rgb = clamp(color + desaturate(dynlight).rgb, 0.0, 1.4);
		specular.rgb = clamp(desaturate(specular).rgb, 0.0, 1.4);
		
	#elif defined(LIGHT_BLEND_COLORED_CLAMP)
		
		dynlight.rgb = color + desaturate(dynlight).rgb;
		specular.rgb = desaturate(specular).rgb;

		dynlight.rgb = ((dynlight.rgb / max(max(max(dynlight.r, dynlight.g), dynlight.b), 1.4) * 1.4));
		specular.rgb = ((specular.rgb / max(max(max(specular.r, specular.g), specular.b), 1.4) * 1.4));
		
	#else // elif defined(LIGHT_BLEND_UNCLAMPED)
		
		dynlight.rgb = color + desaturate(dynlight).rgb;
		specular.rgb = desaturate(specular).rgb;
		
	#endif

	vec3 frag = material.Base.rgb * dynlight.rgb + material.Specular * specular.rgb;

	if (uLightIndex >= 0)
	{
		ivec4 lightRange = getLightRange();
		if (lightRange.w > lightRange.z)
		{
			vec4 addlight = vec4(0.0,0.0,0.0,0.0);

			// additive lights
			for(int i=lightRange.z; i<lightRange.w; i++)
			{
				vec3 lightcolor = getLights()[i].color.rgb;
				vec2 attenuation = lightAttenuation(getLights()[i], normal, viewdir, material.Glossiness, material.SpecularLevel);
				addlight.rgb += lightcolor.rgb * attenuation.x;
			}

			frag = clamp(frag + desaturate(addlight).rgb, 0.0, 1.0);
		}
	}

	return frag;
}
