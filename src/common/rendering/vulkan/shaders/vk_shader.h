
#pragma once

#include <memory>
#include <list>
#include <map>
#include <mutex>
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
			uint64_t Unused0 : 1;
			uint64_t Unused1 : 1;
			uint64_t Simple2D : 1;      // SIMPLE2D, uFogEnabled == -3
			uint64_t Unused2 : 1;
			uint64_t TextureMode : 3;   // uTextureMode & 0xffff
			uint64_t ClampY : 1;        // uTextureMode & TEXF_ClampY
			uint64_t Brightmap : 1;     // uTextureMode & TEXF_Brightmap
			uint64_t Detailmap : 1;     // uTextureMode & TEXF_Detailmap
			uint64_t Glowmap : 1;       // uTextureMode & TEXF_Glowmap
			uint64_t Unused3 : 1;
			uint64_t UseShadowmap : 1;  // USE_SHADOWMAP
			uint64_t UseRaytrace : 1;   // USE_RAYTRACE
			uint64_t Unused4 : 1;
			uint64_t Unused5 : 1; // formerly PRECISE_MIDTEXTURES
			uint64_t ShadowmapFilter : 4; // SHADOWMAP_FILTER
			uint64_t FogBeforeLights : 1; // FOG_BEFORE_LIGHTS
			uint64_t FogAfterLights : 1;  // FOG_AFTER_LIGHTS
			uint64_t FogRadial : 1;       // FOG_RADIAL
			uint64_t SWLightRadial : 1; // SWLIGHT_RADIAL
			uint64_t SWLightBanded : 1; // SWLIGHT_BANDED
			uint64_t LightMode : 2;     // LIGHTMODE_DEFAULT, LIGHTMODE_SOFTWARE, LIGHTMODE_VANILLA, LIGHTMODE_BUILD
			uint64_t LightBlendMode : 2; // LIGHT_BLEND_CLAMPED , LIGHT_BLEND_COLORED_CLAMP , LIGHT_BLEND_UNCLAMPED
			uint64_t LightAttenuationMode : 1; // LIGHT_ATTENUATION_LINEAR , LIGHT_ATTENUATION_INVERSE_SQUARE
			uint64_t PaletteMode: 1;
			uint64_t FogBalls : 1;      // FOGBALLS
			uint64_t NoFragmentShader : 1;
			uint64_t DepthFadeThreshold : 1;
			uint64_t AlphaTestOnly : 1; // ALPHATEST_ONLY
			uint64_t Unused6 : 1;
			uint64_t LightNoNormals : 1; // LIGHT_NONORMALS
			uint64_t UseSpriteCenter : 1; // USE_SPRITE_CENTER
			uint64_t Unused : 26;
		};
		uint64_t AsQWORD = 0;
	};

	int SpecialEffect = 0;
	int EffectState = 0;
	int VertexFormat = 0;

	union
	{
		struct
		{
			uint32_t AlphaTest : 1;     // !NO_ALPHATEST
			uint32_t Simple : 1;        // SIMPLE
			uint32_t Simple3D : 1;      // SIMPLE3D
			uint32_t GBufferPass : 1;   // GBUFFER_PASS
			uint32_t UseLevelMesh : 1;  // USE_LEVELMESH
			uint32_t ShadeVertex : 1;   // SHADE_VERTEX
			uint32_t UseRaytracePrecise : 1; // USE_RAYTRACE_PRECISE
			uint32_t Unused : 25;
		};
		uint32_t AsDWORD = 0;
	} Layout;

	inline uint64_t GeneralizedShaderKey() const
	{
		return uint64_t(Layout.AsDWORD) |
			(uint64_t(EffectState) << 32) |
			((uint64_t(SpecialEffect) & 0xFF) << (32 + 16)) |
			((uint64_t(VertexFormat) & 0xFF)  << (32 + 16 + 8));
	}

	bool operator<(const VkShaderKey& other) const { return memcmp(this, &other, sizeof(VkShaderKey)) < 0; }
	bool operator==(const VkShaderKey& other) const { return memcmp(this, &other, sizeof(VkShaderKey)) == 0; }
	bool operator!=(const VkShaderKey& other) const { return memcmp(this, &other, sizeof(VkShaderKey)) != 0; }
};

static_assert(sizeof(VkShaderKey) == 24, "sizeof(VkShaderKey) is not its expected size!"); // If this assert fails, the flags union no longer adds up to 64 bits. Or there are gaps in the class so the memcmp doesn't work.

class VkShaderProgram
{
public:
	std::vector<uint32_t> vert;
	std::vector<uint32_t> frag;

	UniformStructHolder Uniforms;
};

class VkShaderManager
{
public:
	VkShaderManager(VulkanRenderDevice* fb);
	~VkShaderManager();

	void Deinit();

	VkShaderProgram* GetProgram(const VkShaderKey& key, bool isUberShader);

	bool CompileNextShader() { return true; }

	VkPPShader* GetVkShader(PPShader* shader);

	void AddVkPPShader(VkPPShader* shader);
	void RemoveVkPPShader(VkPPShader* shader);

	const std::vector<uint32_t>& GetZMinMaxVertexShader() const { return ZMinMax.vert; }
	const std::vector<uint32_t>& GetZMinMaxFragmentShader(int index) const { return ZMinMax.frag[index]; }
	const std::vector<uint32_t>& GetLightTilesShader() const { return LightTiles; }

private:
	VkShaderProgram* GetFromCache(const VkShaderKey& key, bool isUberShader);
	VkShaderProgram* AddToCache(const VkShaderKey& key, bool isUberShader, std::unique_ptr<VkShaderProgram> program);
	std::unique_ptr<VkShaderProgram> CompileProgram(const VkShaderKey& key, bool isUberShader);

	std::vector<uint32_t> LoadVertShader(FString shadername, const char *vert_lump, const char *vert_lump_custom, const char *defines, const VkShaderKey& key, const UserShaderDesc *shader, bool isUberShader);
	std::vector<uint32_t> LoadFragShader(FString shadername, const char *frag_lump, const char *material_lump, const char* mateffect_lump, const char *light_lump_shared, const char *lightmodel_lump, const char *defines, const VkShaderKey& key, const UserShaderDesc *shader, bool isUberShader);

	FString GetVersionBlock();
	FString LoadPublicShaderLump(const char *lumpname);
	FString LoadPrivateShaderLump(const char *lumpname);

	void BuildLayoutBlock(FString &definesBlock, bool isFrag, const VkShaderKey& key, const UserShaderDesc *shader, bool isUberShader);
	void BuildDefinesBlock(FString &definesBlock, const char *defines, bool isFrag, const VkShaderKey& key, const UserShaderDesc *shader, bool isUberShader);

	VulkanRenderDevice* fb = nullptr;

	std::mutex mutex;
	std::map<uint64_t, std::unique_ptr<VkShaderProgram>> generic;
	std::map<VkShaderKey, std::unique_ptr<VkShaderProgram>> specialized;

	std::list<VkPPShader*> PPShaders;

	struct
	{
		std::vector<uint32_t> vert;
		std::vector<uint32_t> frag[3];
	} ZMinMax;

	std::vector<uint32_t> LightTiles;
};
