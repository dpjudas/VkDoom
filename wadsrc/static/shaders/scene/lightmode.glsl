
#include "shaders/scene/lightmode_default.glsl"
#include "shaders/scene/lightmode_software.glsl"
#include "shaders/scene/lightmode_vanilla.glsl"
#include "shaders/scene/lightmode_build.glsl"
#include "shaders/scene/material.glsl"
#include "shaders/scene/fogball.glsl"

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
		int light = clamp(int((shade - vis) * 32), 0, 31);

		vec3 matColor = texelFetch(textures[uColormapIndex], ivec2(color, 32), 0).rgb;
		vec4 frag = vec4(texelFetch(textures[uColormapIndex], ivec2(color, light), 0).rgb, material.Base.a * vColor.a);

		vec4 dynlight = uDynLightColor;

		float sunlightAttenuation = 0.0;
		if (vLightmapIndex != -1)
		{
			vec4 lightmap = texture(textures[nonuniformEXT(vLightmapIndex)], vLightmap.xy);
			dynlight.rgb += lightmap.rgb;
			sunlightAttenuation = lightmap.a;
		}

		dynlight.rgb += ProcessSWLight(material, sunlightAttenuation);

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
		float sunlightAttenuation = 0.0;
		if (vLightmapIndex != -1)
		{
			vec4 lightmap = texture(textures[nonuniformEXT(vLightmapIndex)], vLightmap.xy);
			color.rgb += lightmap.rgb;
			sunlightAttenuation = lightmap.a;
		}

		// Force sunlight flag
		if (uDynLightColor.w == -1)
			sunlightAttenuation = 1.0;

		//
		// apply dynamic lights
		//
		vec4 frag = vec4(ProcessMaterialLight(material, color.rgb, sunlightAttenuation), material.Base.a * vColor.a);

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
