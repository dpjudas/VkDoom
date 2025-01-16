#ifndef SIMPLE3D
	vec3 lightContribution(DynLightInfo light, vec3 normal)
	{
		float lightdistance = distance(light.pos.xyz, pixelpos.xyz);
		
		if (light.radius < lightdistance)
			return vec3(0.0); // Early out lights touching surface but not this fragment

		vec3 lightdir = normalize(light.pos.xyz - pixelpos.xyz);
		float dotprod = dot(normal, lightdir);
		if (dotprod < -0.0001) return vec3(0.0);	// light hits from the backside. This can happen with full sector light lists and must be rejected for all cases. Note that this can cause precision issues.
		
		float attenuation = distanceAttenuation(lightdistance, light.radius, light.strength, light.linearity);

		if ((light.flags & LIGHTINFO_SPOT) != 0)
		{
			attenuation *= spotLightAttenuation(light.pos.xyz, light.spotDir.xyz, light.spotInnerAngle, light.spotOuterAngle);
		}

		if ((light.flags & LIGHTINFO_ATTENUATED) != 0)
		{
			attenuation *= clamp(dotprod, 0.0, 1.0);
		}
		

		if (attenuation > 0.0) // Skip shadow map test if possible
		{
			// light.radius >= 1000000.0 is sunlight(?), skip attenuation
			if(light.radius < 1000000.0 && (light.flags & LIGHTINFO_SHADOWMAPPED) != 0)
			{
				attenuation *= shadowAttenuation(light.pos.xyz, light.shadowIndex, light.softShadowRadius);
			}
			
			return light.color.rgb * attenuation;
		}
		else
		{
			return vec3(0.0);
		}
	}
	
	vec3 ProcessMaterialLight(Material material, vec3 color)
	{
		vec4 dynlight = uDynLightColor;
		vec3 normal = material.Normal;
		
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
		
		#ifdef LIGHT_BLEND_CLAMPED
			
			vec3 frag = material.Base.rgb * clamp(color + desaturate(dynlight).rgb, 0.0, 1.4);
			
		#elif defined(LIGHT_BLEND_COLORED_CLAMP)
			
			vec3 frag = color + desaturate(dynlight).rgb;
			frag = material.Base.rgb * ((frag / max(max(max(frag.r, frag.g), frag.b), 1.4) * 1.4));
			
		#else // elif defined(LIGHT_BLEND_UNCLAMPED)
			
			vec3 frag = material.Base.rgb * (color + desaturate(dynlight).rgb);
			
		#endif


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

		return frag;
	}
#else
	vec3 ProcessMaterialLight(Material material, vec3 color)
	{
		return material.Base.rgb;
	}
#endif