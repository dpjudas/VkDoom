
#include "shaders/scene/bones.glsl"

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

	#ifndef SIMPLE
		vLightmap = vec3(aLightmap, aPosition.w);

		pixelpos.xyz = worldcoord.xyz;
		pixelpos.w = -eyeCoordPos.z/eyeCoordPos.w;

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

		if (uSplitBottomPlane.z != 0.0)
		{
			ClipDistance3 = ((uSplitTopPlane.w + uSplitTopPlane.x * worldcoord.x + uSplitTopPlane.y * worldcoord.z) * uSplitTopPlane.z) - worldcoord.y;
			ClipDistance4 = worldcoord.y - ((uSplitBottomPlane.w + uSplitBottomPlane.x * worldcoord.x + uSplitBottomPlane.y * worldcoord.z) * uSplitBottomPlane.z);
		}

		vWorldNormal = NormalModelMatrix * vec4(normalize(bones.Normal), 1.0);
		vEyeNormal = NormalViewMatrix * vec4(normalize(vWorldNormal.xyz), 1.0);
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
    
    ModifyVertex();
}
