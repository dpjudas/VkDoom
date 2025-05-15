
#define SK1_ALPHATEST				(1  << 0)
//#define SK1_SIMPLE				(1  << 1)
#define SK1_SIMPLE2D				(1  << 2)
//#define SK1_SIMPLE3D				(1  << 3)
#define SK1_TEXTUREMODE				(7  << 4)
//	#define SK1_TM_NORMAL				0
	#define SK1_TM_STENCIL				1
	#define SK1_TM_OPAQUE				2
	#define SK1_TM_INVERSE				3
	#define SK1_TM_ALPHATEXTURE			4
	#define SK1_TM_CLAMPY				5
	#define SK1_TM_INVERTOPAQUE			6
	#define SK1_TM_FOGLAYER				7
#define SK1_TEXF_CLAMPY				(1  << 7)
#define SK1_TEXF_BRIGHTMAP			(1  << 8)
#define SK1_TEXF_DETAILMAP			(1  << 9)
#define SK1_TEXF_GLOWMAP			(1  << 10)
#define SK1_GBUFFER_PASS			(1  << 11)
#define SK1_USE_SHADOWMAP			(1  << 12)
#define SK1_USE_RAYTRACE			(1  << 13)
#define SK1_USE_RAYTRACE_PRECISE	(1  << 14)

#define SK1_SHADOWMAP_FILTER		(15 << 16)
#define SK1_FOG_BEFORE_LIGHTS		(1  << 20)
#define SK1_FOG_AFTER_LIGHTS		(1  << 21)
#define SK1_FOG_RADIAL				(1  << 22)
#define SK1_SWLIGHT_RADIAL			(1  << 23)
#define SK1_SWLIGHT_BANDED			(1  << 24)
#define SK1_LIGHTMODE				(3  << 25)
	#define SK1_LIGHTMODE_DEFAULT		0
	#define SK1_LIGHTMODE_SOFTWARE		1
	#define SK1_LIGHTMODE_VANILLA		2
	#define SK1_LIGHTMODE_BUILD			3
#define SK1_LIGHTBLENDMODE			(3  << 27)
	#define SK1_LIGHT_BLEND_CLAMPED					0
	#define SK1_LIGHT_LIGHT_BLEND_COLORED_CLAMP		1
	#define SK1_LIGHT_BLEND_UNCLAMPED				2
#define SK1_LIGHTATTENUATIONMODE	(1  << 29)
//#define SK1_USE_LEVELMESH			(1  << 30)
#define SK1_FOGBALLS				(1  << 31)

//#define SK2_NOFRAGMENTSHADER		(1  << 0)
#define SK2_USE_DEPTHFADETHRESHOLD	(1  << 1)
#define SK2_ALPHATEST_ONLY			(1  << 2)
//#define SK2_SHADE_VERTEX			(1  << 3)
#define SK2_LIGHT_NONORMALS			(1  << 4)
#define SK2_USE_SPRITECENTER		(1  << 5)

#define SK_GET_TEXTUREMODE() ((uShaderKey1 & SK1_TEXTUREMODE) >> 4)
#define SK_GET_SHADOWMAP_FILTER() ((uShaderKey1 & SK1_SHADOWMAP_FILTER) >> 16)
#define SK_GET_LIGHTMODE() ((uShaderKey1 & SK1_LIGHTMODE) >> 25)
#define SK_GET_LIGHTBLENDMODE() ((uShaderKey1 & SK1_LIGHTBLENDMODE) >> 27)

