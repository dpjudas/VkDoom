
#pragma once

#include <memory>
#include <list>
#include <map>
#include "vectors.h"
#include "matrix.h"
#include "name.h"
#include "hw_renderstate.h"
#include "hw_dynlightdata.h"
#include "hwrenderer/postprocessing/hw_useruniforms.h"

class ShaderIncludeResult;
class VulkanRenderDevice;
class VulkanDevice;
class VulkanShader;
class VkPPShader;
class PPShader;

struct MatricesUBO
{
	VSMatrix ModelMatrix;
	VSMatrix NormalModelMatrix;
	VSMatrix TextureMatrix;
};

#define MAX_SURFACE_UNIFORMS ((int)(65536 / sizeof(SurfaceUniforms)))

struct SurfaceUniformsUBO
{
	SurfaceUniforms data[MAX_SURFACE_UNIFORMS];
};

struct LightBufferSSO
{
	//TODO deduplicate individual lights
	int lightIndex[MAX_LIGHT_DATA * 4];
	FDynLightInfo lights[MAX_LIGHT_DATA];
};

#define MAX_FOGBALL_DATA ((int)(65536 / sizeof(Fogball)))

struct FogballBufferUBO
{
	Fogball fogballs[MAX_FOGBALL_DATA];
};

struct PushConstants
{
	int uDataIndex; // streamdata index
	int uLightIndex; // dynamic lights
	int uBoneIndexBase; // bone animation
	int uFogballIndex; // fog balls
	uint64_t shaderKey;
	int padding0;
	int padding1;
};

struct ZMinMaxPushConstants
{
	float LinearizeDepthA;
	float LinearizeDepthB;
	float InverseDepthRangeA;
	float InverseDepthRangeB;
};

struct LightTilesPushConstants
{
	FVector2 posToViewA;
	FVector2 posToViewB;
	FVector2 viewportPos;
	FVector2 padding1;
	VSMatrix worldToView;
};

class VkShaderKey
{
public:
	union
	{
		struct
		{
			uint64_t AlphaTest : 1;     // !NO_ALPHATEST
			uint64_t Simple : 1;		// SIMPLE
			uint64_t Simple2D : 1;      // SIMPLE2D, uFogEnabled == -3
			uint64_t Simple3D : 1;		// SIMPLE3D
			uint64_t TextureMode : 3;   // uTextureMode & 0xffff
			uint64_t ClampY : 1;        // uTextureMode & TEXF_ClampY
			uint64_t Brightmap : 1;     // uTextureMode & TEXF_Brightmap
			uint64_t Detailmap : 1;     // uTextureMode & TEXF_Detailmap
			uint64_t Glowmap : 1;       // uTextureMode & TEXF_Glowmap
			uint64_t GBufferPass : 1;   // GBUFFER_PASS
			uint64_t UseShadowmap : 1;  // USE_SHADOWMAP
			uint64_t UseRaytrace : 1;   // USE_RAYTRACE
			uint64_t UseRaytracePrecise : 1; // USE_RAYTRACE_PRECISE
			uint64_t PreciseMidtextureTrace : 1; // PRECISE_MIDTEXTURES
			uint64_t ShadowmapFilter : 4; // SHADOWMAP_FILTER
			uint64_t FogBeforeLights : 1; // FOG_BEFORE_LIGHTS
			uint64_t FogAfterLights : 1;  // FOG_AFTER_LIGHTS
			uint64_t FogRadial : 1;       // FOG_RADIAL
			uint64_t SWLightRadial : 1; // SWLIGHT_RADIAL
			uint64_t SWLightBanded : 1; // SWLIGHT_BANDED
			uint64_t LightMode : 2;     // LIGHTMODE_DEFAULT, LIGHTMODE_SOFTWARE, LIGHTMODE_VANILLA, LIGHTMODE_BUILD
			uint64_t LightBlendMode : 2; // LIGHT_BLEND_CLAMPED , LIGHT_BLEND_COLORED_CLAMP , LIGHT_BLEND_UNCLAMPED
			uint64_t LightAttenuationMode : 1; // LIGHT_ATTENUATION_LINEAR , LIGHT_ATTENUATION_INVERSE_SQUARE
			uint64_t UseLevelMesh : 1;  // USE_LEVELMESH
			uint64_t FogBalls : 1;      // FOGBALLS
			uint64_t NoFragmentShader : 1;
			uint64_t DepthFadeThreshold : 1;
			uint64_t AlphaTestOnly : 1; // ALPHATEST_ONLY
			uint64_t ShadeVertex : 1; // SHADE_VERTEX
			uint64_t LightNoNormals : 1; // LIGHT_NONORMALS
			uint64_t UseSpriteCenter : 1; // USE_SPRITE_CENTER
			uint64_t Unused : 26;
		};
		uint64_t AsQWORD = 0;
	};

	int SpecialEffect = 0;
	int EffectState = 0;
	int VertexFormat = 0;
	int Padding = 0;

	bool operator<(const VkShaderKey& other) const { return memcmp(this, &other, sizeof(VkShaderKey)) < 0; }
	bool operator==(const VkShaderKey& other) const { return memcmp(this, &other, sizeof(VkShaderKey)) == 0; }
	bool operator!=(const VkShaderKey& other) const { return memcmp(this, &other, sizeof(VkShaderKey)) != 0; }
};

static_assert(sizeof(VkShaderKey) == 24, "sizeof(VkShaderKey) is not its expected size!"); // If this assert fails, the flags union no longer adds up to 64 bits. Or there are gaps in the class so the memcmp doesn't work.

class VkShaderProgram
{
public:
	std::unique_ptr<VulkanShader> vert;
	std::unique_ptr<VulkanShader> frag;

	UniformStructHolder Uniforms;
};

class VkShaderManager
{
public:
	VkShaderManager(VulkanRenderDevice* fb);
	~VkShaderManager();

	void Deinit();

	VkShaderProgram* Get(const VkShaderKey& key);

	bool CompileNextShader() { return true; }

	VkPPShader* GetVkShader(PPShader* shader);

	void AddVkPPShader(VkPPShader* shader);
	void RemoveVkPPShader(VkPPShader* shader);

	VulkanShader* GetZMinMaxVertexShader() { return ZMinMax.vert.get(); }
	VulkanShader* GetZMinMaxFragmentShader(int index) { return ZMinMax.frag[index].get(); }
	VulkanShader* GetLightTilesShader() { return LightTiles.get(); }

private:
	std::unique_ptr<VulkanShader> LoadVertShader(FString shadername, const char *vert_lump, const char *vert_lump_custom, const char *defines, const VkShaderKey& key, const UserShaderDesc *shader);
	std::unique_ptr<VulkanShader> LoadFragShader(FString shadername, const char *frag_lump, const char *material_lump, const char* mateffect_lump, const char *light_lump_shared, const char *lightmodel_lump, const char *defines, const VkShaderKey& key, const UserShaderDesc *shader);

	FString GetVersionBlock();
	FString LoadPublicShaderLump(const char *lumpname);
	FString LoadPrivateShaderLump(const char *lumpname);

	static FString SubstituteDefines(FString code, bool isUberShader = false);
	
	void BuildLayoutBlock(FString &definesBlock, bool isFrag, const VkShaderKey& key, const UserShaderDesc *shader, bool isUberShader = false);
	void BuildDefinesBlock(FString &definesBlock, const char *defines, bool isFrag, const VkShaderKey& key, const UserShaderDesc *shader, bool isUberShader = false);

	VulkanRenderDevice* fb = nullptr;

	std::map<VkShaderKey, VkShaderProgram> programs;

	std::list<VkPPShader*> PPShaders;

	struct
	{
		std::unique_ptr<VulkanShader> vert;
		std::unique_ptr<VulkanShader> frag[3];
	} ZMinMax;

	std::unique_ptr<VulkanShader> LightTiles;
};
