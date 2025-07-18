
#include "shaders/scene/bones.glsl"

#if defined(SHADE_VERTEX) && !defined(PBR) && !defined(SPECULAR) && !defined(SIMPLE)

	#undef SHADOWMAP_FILTER
	#define SHADOWMAP_FILTER 0
	#include <shaders/scene/lightmodel_shared.glsl>
	#include <shaders/scene/light_trace.glsl>
	#include <shaders/scene/light_spot.glsl>
	
	vec3 lightValue(DynLightInfo light)
	{
		float lightdistance;
		vec3 lightdir;

		if (USE_SPRITE_CENTER)
		{
			lightdistance = distance(light.pos.xyz, uActorCenter.xyz);

			if (light.radius < lightdistance)
				return vec3(0.0); // Early out lights touching surface but not this fragment

			lightdir = normalize(light.pos.xyz - uActorCenter.xyz);
		}
		else
		{
			lightdistance = distance(light.pos.xyz, pixelpos.xyz);
		
			if (light.radius < lightdistance)
				return vec3(0.0); // Early out lights touching surface but not this fragment
		
			lightdir = normalize(light.pos.xyz - pixelpos.xyz);
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
				float dotprod = dot(vWorldNormal.xyz, lightdir);
				attenuation *= clamp(dotprod, 0.0, 1.0);
			}
		}
		
		if (attenuation > 0.0) // Skip shadow map test if possible
		{
			if((light.flags & LIGHTINFO_SUN) != 0)
			{
				attenuation *= traceSun(lightdir);
			}
			else if((light.flags & LIGHTINFO_SHADOWMAPPED) != 0)
			{
				attenuation *= traceShadow(light.pos.xyz, light.softShadowRadius);
			}
			
			return light.color.rgb * attenuation;
		}
		else
		{
			return vec3(0.0);
		}
		
		return vec3(0.0);
	}
	
	vec3 ProcessVertexLight()
	{
		vec3 light = vec3(0.0);
		
		if (uLightIndex >= 0)
		{
			ivec4 lightRange = getLightRange();
			
			if (lightRange.z > lightRange.x)
			{
				// modulated lights
				for(int i=lightRange.x; i<lightRange.y; i++)
				{
					light += lightValue(getLights()[i]);
				}
				
				// subtractive lights
				for(int i=lightRange.y; i<lightRange.z; i++)
				{
					light -= lightValue(getLights()[i]);
				}
			}
		}
		
		return light;
	}

#endif

void ModifyVertex();

void main()
{
	float ClipDistance0, ClipDistance1, ClipDistance2, ClipDistance3, ClipDistance4;

	vec2 parmTexCoord;
	vec4 parmPosition;

#if defined(USE_LEVELMESH)
	uDataIndex = aDataIndex;
#endif

	BonesResult bones = ApplyBones();

	parmTexCoord = aTexCoord;
	parmPosition = bones.Position;

	#ifndef SIMPLE
		vec4 worldcoord = ModelMatrix * mix(parmPosition, aVertex2, uInterpolationFactor);
	#else
		vec4 worldcoord = ModelMatrix * parmPosition;
	#endif

	vec4 eyeCoordPos = ViewMatrix * worldcoord;

	#ifdef HAS_UNIFORM_VERTEX_DATA
		if ((useVertexData & 1) == 0)
			vColor = uVertexColor;
		else
			vColor = aColor;
	#else
		vColor = aColor;
	#endif

	#if !defined(SIMPLE) || defined(SIMPLE3D)
		vLightmap = aLightmap;
		if (aPosition.w >= 0.0)
			vLightmapIndex = LightmapsStart + int(aPosition.w) * 2;
		else
			vLightmapIndex = -1;

		pixelpos.xyz = worldcoord.xyz;
		pixelpos.w = -eyeCoordPos.z/eyeCoordPos.w;

		#if !defined(SIMPLE)
			if (uGlowTopColor.a > 0 || uGlowBottomColor.a > 0)
			{
				float topatpoint = (uGlowTopPlane.w + uGlowTopPlane.x * worldcoord.x + uGlowTopPlane.y * worldcoord.z) * uGlowTopPlane.z;
				float bottomatpoint = (uGlowBottomPlane.w + uGlowBottomPlane.x * worldcoord.x + uGlowBottomPlane.y * worldcoord.z) * uGlowBottomPlane.z;
				glowdist.x = topatpoint - worldcoord.y;
				glowdist.y = worldcoord.y - bottomatpoint;
				glowdist.z = clamp(glowdist.x / (topatpoint - bottomatpoint), 0.0, 1.0);
			}

			if (uObjectColor2.a != 0)
			{
				float topatpoint = (uGradientTopPlane.w + uGradientTopPlane.x * worldcoord.x + uGradientTopPlane.y * worldcoord.z) * uGradientTopPlane.z;
				float bottomatpoint = (uGradientBottomPlane.w + uGradientBottomPlane.x * worldcoord.x + uGradientBottomPlane.y * worldcoord.z) * uGradientBottomPlane.z;
				gradientdist.x = topatpoint - worldcoord.y;
				gradientdist.y = worldcoord.y - bottomatpoint;
				gradientdist.z = clamp(gradientdist.x / (topatpoint - bottomatpoint), 0.0, 1.0);
			}
		#endif

		if (uSplitBottomPlane.z != 0.0)
		{
			ClipDistance3 = ((uSplitTopPlane.w + uSplitTopPlane.x * worldcoord.x + uSplitTopPlane.y * worldcoord.z) * uSplitTopPlane.z) - worldcoord.y;
			ClipDistance4 = worldcoord.y - ((uSplitBottomPlane.w + uSplitBottomPlane.x * worldcoord.x + uSplitBottomPlane.y * worldcoord.z) * uSplitBottomPlane.z);
		}

		vWorldNormal = vec4(normalize((NormalModelMatrix * vec4(normalize(bones.Normal), 1.0)).xyz), 1.0);
		vEyeNormal = vec4(normalize((NormalViewMatrix * vec4(normalize(vWorldNormal.xyz), 1.0)).xyz), 1.0);
	#endif
	
	#ifdef SPHEREMAP
		vec3 u = normalize(eyeCoordPos.xyz);
		vec4 n = normalize(NormalViewMatrix * vec4(parmTexCoord.x, 0.0, parmTexCoord.y, 0.0));
		vec3 r = reflect(u, n.xyz);
		float m = 2.0 * sqrt( r.x*r.x + r.y*r.y + (r.z+1.0)*(r.z+1.0) );
		vec2 sst = vec2(r.x/m + 0.5,  r.y/m + 0.5);
		vTexCoord.xy = sst;
	#else
		vTexCoord = TextureMatrix * vec4(parmTexCoord, 0.0, 1.0);
	#endif

	gl_Position = ProjectionMatrix * eyeCoordPos;

	#ifdef VULKAN_COORDINATE_SYSTEM
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
	#endif

	if (uClipHeightDirection != 0.0) // clip planes used for reflective flats
	{
		ClipDistance0 = (worldcoord.y - uClipHeight) * uClipHeightDirection;
	}
	else if (uClipLine.x > -1000000.0) // and for line portals - this will never be active at the same time as the reflective planes clipping so it can use the same hardware clip plane.
	{
		ClipDistance0 = -( (worldcoord.z - uClipLine.y) * uClipLine.z + (uClipLine.x - worldcoord.x) * uClipLine.w ) + 1.0/32768.0;	// allow a tiny bit of imprecisions for colinear linedefs.
	}
	else
	{
		ClipDistance0 = 1;
	}

	// clip planes used for translucency splitting
	ClipDistance1 = worldcoord.y - uClipSplit.x;
	ClipDistance2 = uClipSplit.y - worldcoord.y;

	if (uSplitTopPlane == vec4(0.0))
	{
		ClipDistance3 = 1.0;
		ClipDistance4 = 1.0;
	}

#ifdef NO_CLIPDISTANCE_SUPPORT
	ClipDistanceA = vec4(ClipDistance0, ClipDistance1, ClipDistance2, ClipDistance3);
	ClipDistanceB = vec4(ClipDistance4, 0.0, 0.0, 0.0);
#else
	gl_ClipDistance[0] = ClipDistance0;
	gl_ClipDistance[1] = ClipDistance1;
	gl_ClipDistance[2] = ClipDistance2;
	gl_ClipDistance[3] = ClipDistance3;
	gl_ClipDistance[4] = ClipDistance4;
#endif

	gl_PointSize = 1.0;
	
	#if defined(SHADE_VERTEX) && !defined(PBR) && !defined(SPECULAR) && !defined(SIMPLE)
		vLightColor = ProcessVertexLight();
	#endif
	
	ModifyVertex();
}
