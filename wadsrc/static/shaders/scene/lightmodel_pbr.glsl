const float PI = 3.14159265359;
const float PBRBrightnessScale = 2.5; // For making non-PBR and PBR lights roughly the same intensity

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float NdotH2 = NdotH*NdotH;

	float nom = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;

	float nom = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);
	return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 ProcessLight(const DynLightInfo light, vec3 albedo, float metallic, float roughness, const vec3 N, const vec3 V, const vec3 F0)
{
	vec3 L = normalize(light.pos.xyz - pixelpos.xyz);
	vec3 H = normalize(V + L);

	float attenuation = distanceAttenuation(distance(light.pos.xyz, pixelpos.xyz), light.radius, light.strength, light.linearity);
	if ((light.flags & LIGHTINFO_SPOT) != 0)
	{
		attenuation *= spotLightAttenuation(light.pos.xyz, light.spotDir.xyz, light.spotInnerAngle, light.spotOuterAngle);
	}
	
	if ((light.flags & LIGHTINFO_ATTENUATED) != 0)
	{
		attenuation *= clamp(dot(N, L), 0.0, 1.0);
	}

	if (attenuation > 0.0)
	{
		// light.radius >= 1000000.0 is sunlight(?), skip attenuation
		
		if(light.radius < 1000000.0 && (light.flags & LIGHTINFO_SHADOWMAPPED) != 0)
		{
			attenuation *= shadowAttenuation(light.pos.xyz, light.shadowIndex, light.softShadowRadius, light.flags);
		}
		
		vec3 radiance = light.color.rgb * attenuation * PBRBrightnessScale;
		
		// cook-torrance brdf
		float NDF = DistributionGGX(N, H, roughness);
		float G = GeometrySmith(N, V, L, roughness);
		vec3 F = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);
		
		vec3 kS = F;
		vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
		
		vec3 nominator = NDF * G * F;
		float denominator = 4.0 * clamp(dot(N, V), 0.0, 1.0) * clamp(dot(N, L), 0.0, 1.0);
		vec3 specular = nominator / max(denominator, 0.001);
		
		return (kD * albedo / PI + specular) * radiance;
	}
	
	return vec3(0.0);
}

vec3 ProcessMaterialLight(Material material, vec3 ambientLight, float sunlightAttenuation)
{
	vec3 albedo = material.Base.rgb;

	float metallic = material.Metallic;
	float roughness = material.Roughness;
	float ao = material.AO;

	vec3 N = material.Normal;
	vec3 V = normalize(uCameraPos.xyz - pixelpos.xyz);

	vec3 F0 = mix(vec3(0.04), albedo, metallic);

	vec3 Lo = uDynLightColor.rgb;

	if (sunlightAttenuation > 0.0)
	{
		vec3 L = SunDir;
		vec3 H = normalize(V + L);

		sunlightAttenuation *= clamp(dot(N, L), 0.0, 1.0);

		vec3 radiance = SunColor * SunIntensity * PBRBrightnessScale * sunlightAttenuation;
		
		// cook-torrance brdf
		float NDF = DistributionGGX(N, H, roughness);
		float G = GeometrySmith(N, V, L, roughness);
		vec3 F = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);
		vec3 kS = F;
		vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
		vec3 nominator = NDF * G * F;
		float denominator = 4.0 * clamp(dot(N, V), 0.0, 1.0) * clamp(dot(N, L), 0.0, 1.0);
		vec3 specular = nominator / max(denominator, 0.001);
		Lo += (kD * albedo / PI + specular) * radiance;
	}

#ifndef UBERSHADER
	if (uLightIndex >= 0)
	{
		ivec4 lightRange = getLightRange();
		if (lightRange.z > lightRange.x)
		{
			//
			// modulated lights
			//
			for(int i = lightRange.x; i < lightRange.y; i++)
			{
				Lo += ProcessLight(getLights()[i], albedo, metallic, roughness, N, V, F0);
			}
			//
			// subtractive lights
			//
			for(int i = lightRange.y; i < lightRange.z; i++)
			{
				Lo -= ProcessLight(getLights()[i], albedo, metallic, roughness, N, V, F0);
			}
		}
	}
#endif

	// Treat the ambient sector light as if it is a light source next to the wall
	{
		vec3 VV = V;
		vec3 LL = N;
		vec3 HH = normalize(VV + LL);

		vec3 radiance = ambientLight.rgb * 2.25;

		vec3 F = fresnelSchlick(clamp(dot(HH, VV), 0.0, 1.0), F0);
		vec3 kS = F;
		vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
		vec3 specular = metallic * albedo * 0.40;

		Lo += (kD * albedo / PI + specular) * radiance;
	}

	vec3 F = fresnelSchlickRoughness(clamp(dot(N, V), 0.0, 1.0), F0, roughness);

	vec3 kS = F;
	vec3 kD = 1.0 - kS;

	const float MAX_REFLECTION_LOD = 4.0;
	vec3 R = reflect(-V, N); 

	vec3 irradiance, prefilteredColor;

	if (vLightmapIndex != -1 && uLightProbeIndex == 0)
	{
		uvec4 probeIndexes = textureGather(uintTextures[nonuniformEXT(vLightmapIndex + 1)], vLightmap.xy);

		vec2 t = fract(vLightmap.xy);
		vec2 invt = 1.0 - t;
		float t00 = invt.x * invt.y;
		float t10 = t.x * invt.y;
		float t01 = invt.x * t.y;
		float t11 = t.x * t.y;

		vec3 irradiance0 = texture(cubeTextures[probeIndexes.x], N).rgb;
		vec3 irradiance1 = texture(cubeTextures[probeIndexes.y], N).rgb;
		vec3 irradiance2 = texture(cubeTextures[probeIndexes.z], N).rgb;
		vec3 irradiance3 = texture(cubeTextures[probeIndexes.w], N).rgb;
		
		vec3 prefilteredColor0 = textureLod(cubeTextures[probeIndexes.x + 1], R, roughness * MAX_REFLECTION_LOD).rgb;
		vec3 prefilteredColor1 = textureLod(cubeTextures[probeIndexes.y + 1], R, roughness * MAX_REFLECTION_LOD).rgb;
		vec3 prefilteredColor2 = textureLod(cubeTextures[probeIndexes.z + 1], R, roughness * MAX_REFLECTION_LOD).rgb;
		vec3 prefilteredColor3 = textureLod(cubeTextures[probeIndexes.w + 1], R, roughness * MAX_REFLECTION_LOD).rgb;

		irradiance = irradiance0 * t00 + irradiance1 * t10 + irradiance2 * t01 + irradiance3 * t11;
		prefilteredColor = prefilteredColor0 * t00 + prefilteredColor1 * t10 + prefilteredColor2 * t01 + prefilteredColor3 * t11;
	}
	else
	{
		irradiance = texture(cubeTextures[uLightProbeIndex], N).rgb;
		prefilteredColor = textureLod(cubeTextures[uLightProbeIndex + 1], R, roughness * MAX_REFLECTION_LOD).rgb;
	}

	/*
	const float environmentScaleFactor = 1.0;
	prefilteredColor *= environmentScaleFactor;
	irradiance *= environmentScaleFactor;
	*/

	vec2 envBRDF = texture(textures[BrdfLUT], vec2(clamp(dot(N, V), 0.0, 1.0), roughness)).rg;
	vec3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

	vec3 diffuse = irradiance * albedo;
	kD *= 1.0 - metallic;

	vec3 ambient = (kD * diffuse + specular) * ao;

	vec3 color = max(ambient + Lo, vec3(0.0));
	return color;
}

vec3 ProcessSWLight(Material material, float sunlightAttenuation)
{
	return vec3(0.0);
}
