
#include "shaders/scene/lightmode_default.glsl"
#include "shaders/scene/lightmode_software.glsl"
#include "shaders/scene/lightmode_vanilla.glsl"
#include "shaders/scene/lightmode_build.glsl"
#include "shaders/scene/material.glsl"
#include "shaders/scene/fogball.glsl"

#ifndef SIMPLE3D
vec3 swLightContribution(DynLightInfo light, vec3 normal);
float distanceAttenuation(float dist, float radius, float strength, float linearity);
float spotLightAttenuation(vec3 lightpos, vec3 spotdir, float lightCosInnerAngle, float lightCosOuterAngle);
float traceSun(vec3 SunDir);
float shadowAttenuation(vec3 lightpos, int shadowIndex, float softShadowRadius, int flags);
#endif

vec3 PickGamePaletteColor(vec3 color)
{
	ivec3 c = ivec3(clamp(color.rgb, vec3(0.0), vec3(1.0)) * 63.0 + 0.5);
	int index = (c.r * 64 + c.g) * 64 + c.b;
	int tx = index % 512;
	int ty = index / 512;
	return texelFetch(textures[PaletteLUT], ivec2(tx, ty), 0).rgb;
}

//===========================================================================
//
// Calculate light
//
// It is important to note that the light color is not desaturated
// due to ZDoom's implementation weirdness. Everything that's added
// on top of it, e.g. dynamic lights and glows are, though, because
// the objects emitting these lights are also.
//
// This is making this a bit more complicated than it needs to
// because we can't just desaturate the final fragment color.
//
//===========================================================================

vec4 getLightColor(Material material)
{
	if (PALETTEMODE)
	{
		int color = int(material.Base.r * 255.0 + 0.5);

		// z is the depth in view space, positive going into the screen
		float z;
		if (SWLIGHT_RADIAL)
			z = distance(pixelpos.xyz, uCameraPos.xyz);
		else
			z = pixelpos.w;

		float L = uLightLevel * 255.0;
		float vis = min(uGlobVis / z, 24.0 / 32.0);
		float shade = 2.0 - (L + 12.0) / 128.0;
		int light = max(int((shade - vis) * 32), 0);

		vec3 matColor = texelFetch(textures[uColormapIndex], ivec2(color, 0), 0).rgb;
		vec4 frag = vec4(texelFetch(textures[uColormapIndex], ivec2(color, light), 0).rgb, 1.0);

		vec4 dynlight = uDynLightColor;

		if (vLightmap.z >= 0.0)
		{
			dynlight.rgb += texture(LightMap, vLightmap).rgb;
		}

		vec3 normal = material.Normal;

		#ifndef SIMPLE3D
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
								dynlight.rgb += swLightContribution(getLights()[i], normal);
							}

							// subtractive lights
							for(int i=lightRange.y; i<lightRange.z; i++)
							{
								dynlight.rgb -= swLightContribution(getLights()[i], normal);
							}
						}
					}
				#endif
				dynlight = desaturate(dynlight);
			#endif
		#endif

		frag.rgb = PickGamePaletteColor(frag.rgb + matColor * dynlight.rgb);
		return frag;
	}
	else
	{
		vec4 color;
		if (LIGHTMODE_DEFAULT)
			color = Lightmode_Default();
		else if (LIGHTMODE_SOFTWARE)
			color = Lightmode_Software();
		else if (LIGHTMODE_VANILLA)
			color = Lightmode_Vanilla();
		else if (LIGHTMODE_BUILD)
			color = Lightmode_Build();

		//
		// handle glowing walls
		//
		if (uGlowTopColor.a > 0.0 && glowdist.x < uGlowTopColor.a)
		{
			color.rgb += desaturate(uGlowTopColor * (1.0 - glowdist.x / uGlowTopColor.a)).rgb;
		}
		if (uGlowBottomColor.a > 0.0 && glowdist.y < uGlowBottomColor.a)
		{
			color.rgb += desaturate(uGlowBottomColor * (1.0 - glowdist.y / uGlowBottomColor.a)).rgb;
		}
		color = min(color, 1.0);

		// these cannot be safely applied by the legacy format where the implementation cannot guarantee that the values are set.
	#if !defined LEGACY_USER_SHADER && !defined NO_LAYERS
		//
		// apply glow 
		//
		color.rgb = mix(color.rgb, material.Glow.rgb, material.Glow.a);

		//
		// apply brightmaps 
		//
		color.rgb = min(color.rgb + material.Bright.rgb, 1.0);
	#endif

		//
		// apply lightmaps
		//
		if (vLightmap.z >= 0.0)
		{
			color.rgb += texture(LightMap, vLightmap).rgb;
		}

		//
		// apply dynamic lights
		//
		vec4 frag = vec4(ProcessMaterialLight(material, color.rgb), material.Base.a * vColor.a);

		//
		// colored fog
		//
		if (FOG_AFTER_LIGHTS)
		{
			// calculate fog factor
			float fogdist;
			if (FOG_RADIAL)
				fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
			else
				fogdist = max(16.0, pixelpos.w);
			float fogfactor = exp2 (uFogDensity * fogdist);

			frag = vec4(mix(uFogColor.rgb, frag.rgb, fogfactor), frag.a);
		}

		if (FOGBALLS)
			frag = ProcessFogBalls(frag);

		return frag;
	}
}

// The color of the fragment if it is fully occluded by ambient lighting

vec3 AmbientOcclusionColor()
{
	// calculate fog factor
	float fogdist;
	if (FOG_RADIAL)
		fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
	else
		fogdist = max(16.0, pixelpos.w);
	float fogfactor = exp2 (uFogDensity * fogdist);

	vec4 color = vec4(mix(uFogColor.rgb, vec3(0.0), fogfactor), 0.0);

	if (FOGBALLS)
		color = ProcessFogBalls(color);

	return color.rgb;
}

vec4 ProcessLightMode(Material material)
{
	if (SIMPLE2D) // uses the fog color to add a color overlay
	{
		if (TM_FOGLAYER)
		{
			vec4 frag = material.Base;
			float gray = grayscale(frag);
			vec4 cm = (uObjectColor + gray * (uAddColor - uObjectColor)) * 2;
			frag = vec4(clamp(cm.rgb, 0.0, 1.0), frag.a);
			frag *= vColor;
			frag.rgb = frag.rgb + uFogColor.rgb;
			return frag;
		}
		else
		{
			vec4 frag = material.Base * vColor;
			frag.rgb = frag.rgb + uFogColor.rgb;
			return frag;
		}
	}
	else
	{
		if (TM_FOGLAYER)
		{
			if (FOG_BEFORE_LIGHTS || FOG_AFTER_LIGHTS)
			{
				float fogdist;
				if (FOG_RADIAL)
					fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
				else
					fogdist = max(16.0, pixelpos.w);
				float fogfactor = exp2 (uFogDensity * fogdist);

				return vec4(uFogColor.rgb, (1.0 - fogfactor) * material.Base.a * 0.75 * vColor.a);
			}
			else
			{
				return vec4(uFogColor.rgb, material.Base.a * 0.75 * vColor.a);
			}
		}
		else
			return getLightColor(material);
	}
}

#ifndef SIMPLE3D
	vec3 swLightContribution(DynLightInfo light, vec3 normal)
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
#endif
