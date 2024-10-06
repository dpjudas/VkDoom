#include "shaders/scene/lightmodel_shared.glsl"

const float PI = 3.14159265359;

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

vec3 ProcessMaterialLight(Material material, vec3 ambientLight)
{
	vec3 worldpos = pixelpos.xyz;

	vec3 albedo = material.Base.rgb;

	float metallic = material.Metallic;
	float roughness = material.Roughness;
	float ao = material.AO;

	vec3 N = material.Normal;
	vec3 V = normalize(uCameraPos.xyz - worldpos);

	vec3 F0 = mix(vec3(0.04), albedo, metallic);

	vec3 Lo = uDynLightColor.rgb;

	if (uLightIndex >= 0)
	{
		ivec4 lightRange = ivec4(lights[uLightIndex]) + ivec4(uLightIndex + 1);
		if (lightRange.z > lightRange.x)
		{
			//
			// modulated lights
			//
			for(int i=lightRange.x; i<lightRange.y; i+=4)
			{
				vec4 lightpos = lights[i];
				vec4 lightcolor = lights[i+1];
				vec4 lightspot1 = lights[i+2];
				vec4 lightspot2 = lights[i+3];

				vec3 L = normalize(lightpos.xyz - worldpos);
				vec3 H = normalize(V + L);

				float attenuation = distanceAttenuation(distance(lightpos.xyz, pixelpos.xyz), lightpos.w);
				if (lightspot1.w == 1.0)
					attenuation *= spotLightAttenuation(lightpos, lightspot1.xyz, lightspot2.x, lightspot2.y);
				if (lightcolor.a < 0.0)
					attenuation *= clamp(dot(N, L), 0.0, 1.0); // Sign bit is the attenuated light flag

				if (attenuation > 0.0)
				{
					attenuation *= shadowAttenuation(lightpos, lightcolor.a, lightspot2.z);

					vec3 radiance = lightcolor.rgb * attenuation;

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
			}
			//
			// subtractive lights
			//
			for(int i=lightRange.y; i<lightRange.z; i+=4)
			{
				vec4 lightpos = lights[i];
				vec4 lightcolor = lights[i+1];
				vec4 lightspot1 = lights[i+2];
				vec4 lightspot2 = lights[i+3];

				vec3 L = normalize(lightpos.xyz - worldpos);
				vec3 H = normalize(V + L);

				float attenuation = distanceAttenuation(distance(lightpos.xyz, pixelpos.xyz), lightpos.w);
				if (lightspot1.w == 1.0)
					attenuation *= spotLightAttenuation(lightpos, lightspot1.xyz, lightspot2.x, lightspot2.y);
				if (lightcolor.a < 0.0)
					attenuation *= clamp(dot(N, L), 0.0, 1.0); // Sign bit is the attenuated light flag

				if (attenuation > 0.0)
				{
					attenuation *= shadowAttenuation(lightpos, lightcolor.a, lightspot2.z);

					vec3 radiance = lightcolor.rgb * attenuation;

					// cook-torrance brdf
					float NDF = DistributionGGX(N, H, roughness);
					float G = GeometrySmith(N, V, L, roughness);
					vec3 F = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);

					vec3 kS = F;
					vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

					vec3 nominator = NDF * G * F;
					float denominator = 4.0 * clamp(dot(N, V), 0.0, 1.0) * clamp(dot(N, L), 0.0, 1.0);
					vec3 specular = nominator / max(denominator, 0.001);

					Lo -= (kD * albedo / PI + specular) * radiance;
				}
			}
		}
	}

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

	float probeIndex = 0.0; // To do: get this from an uniform

	vec3 F = fresnelSchlickRoughness(clamp(dot(N, V), 0.0, 1.0), F0, roughness);

	vec3 kS = F;
	vec3 kD = 1.0 - kS;

	vec3 irradiance = texture(IrradianceMap, vec4(N, probeIndex)).rgb;
	vec3 diffuse = irradiance * albedo;

	kD *= 1.0 - metallic;
	const float MAX_REFLECTION_LOD = 4.0;
	vec3 R = reflect(-V, N); 
	vec3 prefilteredColor = textureLod(PrefilterMap, vec4(R, probeIndex),  roughness * MAX_REFLECTION_LOD).rgb;
	vec2 envBRDF = texture(BrdfLUT, vec2(clamp(dot(N, V), 0.0, 1.0), roughness)).rg;
	vec3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

	vec3 ambient = (kD * diffuse + specular) * ao;

	vec3 color = max(ambient + Lo, vec3(0.0));
	return color;
}
