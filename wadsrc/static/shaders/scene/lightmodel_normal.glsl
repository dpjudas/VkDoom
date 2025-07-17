
#ifndef SIMPLE3D
	vec3 lightContribution(DynLightInfo light, vec3 normal)
	{
		float lightdistance = distance(light.pos.xyz, pixelpos.xyz);
		
		if (light.radius < lightdistance)
			return vec3(0.0); // Early out lights touching surface but not this fragment
		
		vec3 lightdir = normalize(light.pos.xyz - pixelpos.xyz);
		
		float dotprod;
		
		if (!LIGHT_NONORMALS)
		{
			dotprod = dot(normal, lightdir);
			if (dotprod < -0.0001) return vec3(0.0);	// light hits from the backside. This can happen with full sector light lists and must be rejected for all cases. Note that this can cause precision issues.
		}
		
		float attenuation = distanceAttenuation(lightdistance, light.radius, light.strength, light.linearity);

		if ((light.flags & LIGHTINFO_SPOT) != 0)
		{
			attenuation *= spotLightAttenuation(light.pos.xyz, light.spotDir.xyz, light.spotInnerAngle, light.spotOuterAngle);
		}
		
		if (!LIGHT_NONORMALS)
		{
			if ((light.flags & LIGHTINFO_ATTENUATED) != 0)
			{
				attenuation *= clamp(dotprod, 0.0, 1.0);
			}
		}
		
		if (attenuation > 0.0) // Skip shadow map test if possible
		{
			if((light.flags & (LIGHTINFO_SUN | LIGHTINFO_TRACE)) == (LIGHTINFO_SUN | LIGHTINFO_TRACE))
			{
				attenuation *= traceSun(lightdir);
			}
			else if((light.flags & (LIGHTINFO_SHADOWMAPPED | LIGHTINFO_SUN)) == LIGHTINFO_SHADOWMAPPED)
			{
				attenuation *= shadowAttenuation(light.pos.xyz, light.shadowIndex, light.softShadowRadius, light.flags);
			}
			
			return light.color.rgb * attenuation;
		}
		else
		{
			return vec3(0.0);
		}
	}
	
	vec3 ProcessMaterialLight(Material material, vec3 color, float sunlightAttenuation)
	{
		vec4 dynlight = uDynLightColor;
		vec3 normal = material.Normal;

		if (sunlightAttenuation > 0.0)
		{
			sunlightAttenuation *= clamp(dot(normal, SunDir), 0.0, 1.0);
			dynlight.rgb += SunColor.rgb * SunIntensity * sunlightAttenuation;
		}

		#ifndef UBERSHADER
		
		#ifdef SHADE_VERTEX
			dynlight.rgb += vLightColor;
		#else
			if (uLightIndex >= 0)
			{
				ivec4 lightRange = getLightRange();
				
				if (lightRange.z > lightRange.x)
				{
					// modulated lights
					for(int i=lightRange.x; i<lightRange.y; i++)
					{
						dynlight.rgb += lightContribution(getLights()[i], normal);
					}

					// subtractive lights
					for(int i=lightRange.y; i<lightRange.z; i++)
					{
						dynlight.rgb -= lightContribution(getLights()[i], normal);
					}
				}
			}
		#endif

		#endif
		
		vec3 frag;
		
		if (LIGHT_BLEND_CLAMPED)
		{
			frag = material.Base.rgb * clamp(color + desaturate(dynlight).rgb, 0.0, 1.4);
		}
		else if (LIGHT_BLEND_COLORED_CLAMP)
		{
			frag = color + desaturate(dynlight).rgb;
			frag = material.Base.rgb * ((frag / max(max(max(frag.r, frag.g), frag.b), 1.4) * 1.4));
		}
		else
		{
			frag = material.Base.rgb * (color + desaturate(dynlight).rgb);
		}

		#ifndef UBERSHADER
		#ifndef SHADE_VERTEX
			if (uLightIndex >= 0)
			{
				ivec4 lightRange = getLightRange();
				if (lightRange.w > lightRange.z)
				{
					vec4 addlight = vec4(0.0,0.0,0.0,0.0);

					// additive lights
					for(int i=lightRange.z; i<lightRange.w; i++)
					{
						addlight.rgb += lightContribution(getLights()[i], normal);
					}

					frag = clamp(frag + desaturate(addlight).rgb, 0.0, 1.0);
				}
			}
		#endif
		#endif

		return frag;
	}

	vec3 ProcessSWLight(Material material)
	{
		vec3 normal = material.Normal;
		vec3 dynlight = vec3(0.0);

		#ifndef UBERSHADER
			#ifdef SHADE_VERTEX
				dynlight.rgb += vLightColor;
			#else
				if (uLightIndex >= 0)
				{
					ivec4 lightRange = getLightRange();
				
					if (lightRange.z > lightRange.x)
					{
						// modulated lights
						for(int i=lightRange.x; i<lightRange.y; i++)
						{
							dynlight.rgb += lightContribution(getLights()[i], normal);
						}

						// subtractive lights
						for(int i=lightRange.y; i<lightRange.z; i++)
						{
							dynlight.rgb -= lightContribution(getLights()[i], normal);
						}
					}
				}
			#endif
			dynlight = desaturate(vec4(dynlight, 0.0)).rgb;
		#endif

		return dynlight;
	}

#else
	vec3 ProcessMaterialLight(Material material, vec3 color, float sunlightAttenuation)
	{
		return material.Base.rgb;
	}

	vec3 ProcessSWLight(Material material)
	{
		return vec3(0.0);
	}
#endif
