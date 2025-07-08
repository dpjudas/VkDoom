/*
**  Vulkan backend
**  Copyright (c) 2016-2020 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
*/

#include "vk_shader.h"
#include "vk_ppshader.h"
#include "vk_shadercache.h"
#include "vulkan/vk_renderdevice.h"
#include "vulkan/pipelines/vk_renderpass.h"
#include <zvulkan/vulkanbuilders.h>
#include "hw_shaderpatcher.h"
#include "filesystem.h"
#include "engineerrors.h"
#include "version.h"
#include "cmdlib.h"

VkShaderManager::VkShaderManager(VulkanRenderDevice* fb) : fb(fb)
{
	ZMinMax.vert = CachedGLSLCompiler()
		.Type(ShaderType::Vertex)
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("shaders/scene/vert_zminmax.glsl", LoadPrivateShaderLump("shaders/scene/vert_zminmax.glsl").GetChars())
		.Compile(fb);

	ZMinMax.frag[0] = CachedGLSLCompiler()
		.Type(ShaderType::Fragment)
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("shaders/scene/frag_zminmax0.glsl", LoadPrivateShaderLump("shaders/scene/frag_zminmax0.glsl").GetChars())
		.Compile(fb);

	ZMinMax.frag[1] = CachedGLSLCompiler()
		.Type(ShaderType::Fragment)
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("DefinesBlock", "#define MULTISAMPLE\n")
		.AddSource("shaders/scene/frag_zminmax0.glsl", LoadPrivateShaderLump("shaders/scene/frag_zminmax0.glsl").GetChars())
		.Compile(fb);

	ZMinMax.frag[2] = CachedGLSLCompiler()
		.Type(ShaderType::Fragment)
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("shaders/scene/frag_zminmax1.glsl", LoadPrivateShaderLump("shaders/scene/frag_zminmax1.glsl").GetChars())
		.Compile(fb);

	LightTiles = CachedGLSLCompiler()
		.Type(ShaderType::Compute)
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("shaders/scene/comp_lighttiles.glsl", LoadPrivateShaderLump("shaders/scene/comp_lighttiles.glsl").GetChars())
		.Compile(fb);
}

VkShaderManager::~VkShaderManager()
{
}

void VkShaderManager::Deinit()
{
	while (!PPShaders.empty())
		RemoveVkPPShader(PPShaders.back());
}

VkShaderProgram* VkShaderManager::GetProgram(const VkShaderKey& key, bool isUberShader)
{
	VkShaderProgram* program = GetFromCache(key, isUberShader);
	if (program)
		return program;
	return AddToCache(key, isUberShader, CompileProgram(key, isUberShader));
}

VkShaderProgram* VkShaderManager::GetFromCache(const VkShaderKey& key, bool isUberShader)
{
	std::unique_lock lock(mutex);
	if (isUberShader)
	{
		auto it = generic.find(key.GeneralizedShaderKey());
		if (it != generic.end())
			return it->second.get();
	}
	else
	{
		auto it = specialized.find(key);
		if (it != specialized.end())
			return it->second.get();
	}
	return nullptr;
}

VkShaderProgram* VkShaderManager::AddToCache(const VkShaderKey& key, bool isUberShader, std::unique_ptr<VkShaderProgram> program)
{
	// Multiple threads may be compiling the program (we don't block the cache).
	// If someone beat us to it, use their copy instead.
	std::unique_lock lock(mutex);
	auto& entry = isUberShader ? generic[key.GeneralizedShaderKey()] : specialized[key];
	if (!entry)
		entry = std::move(program);
	return entry.get();
}

std::unique_ptr<VkShaderProgram> VkShaderManager::CompileProgram(const VkShaderKey& key, bool isUberShader)
{
	auto program = std::make_unique<VkShaderProgram>();

	const char* mainvp = "shaders/scene/vert_main.glsl";
	const char* mainfp = "shaders/scene/frag_main.glsl";

	if (key.SpecialEffect != EFF_NONE)
	{
		struct FEffectShader
		{
			const char* ShaderName;
			const char* fp1;
			const char* fp2;
			const char* fp3;
			const char* fp4;
			const char* fp5;
			const char* defines;
		};

		static const FEffectShader effectshaders[] =
		{
			{ "fogboundary",  "shaders/scene/frag_fogboundary.glsl", nullptr,                               nullptr,                                nullptr,                                nullptr,                                "#define NO_ALPHATEST\n" },
			{ "spheremap",    "shaders/scene/frag_main.glsl",        "shaders/scene/material_default.glsl", "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl", "shaders/scene/lightmodel_normal.glsl", "#define SPHEREMAP\n#define NO_ALPHATEST\n" },
			{ "burn",         "shaders/scene/frag_burn.glsl",        nullptr,                               nullptr,                                nullptr,                                nullptr,                                "#define SIMPLE\n#define NO_ALPHATEST\n" },
			{ "stencil",      "shaders/scene/frag_stencil.glsl",     nullptr,                               nullptr,                                nullptr,                                nullptr,                                "#define SIMPLE\n#define NO_ALPHATEST\n" },
			{ "portal",       "shaders/scene/frag_portal.glsl",      nullptr,                               nullptr,                                nullptr,                                nullptr,                                "#define SIMPLE\n#define NO_ALPHATEST\n" },
			{ "dithertrans",  "shaders/scene/frag_main.glsl",        "shaders/scene/material_default.glsl", "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl", "shaders/scene/lightmodel_normal.glsl", "#define NO_ALPHATEST\n#define DITHERTRANS\n" },
		};

		VkShaderKey customKey = key;
		customKey.Layout.AlphaTest = false;

		const auto& desc = effectshaders[key.SpecialEffect];
		program->vert = LoadVertShader(desc.ShaderName, mainvp, nullptr, desc.defines, key, nullptr, isUberShader);
		if (!key.NoFragmentShader)
			program->frag = LoadFragShader(desc.ShaderName, desc.fp1, desc.fp2, desc.fp3, desc.fp4, desc.fp5, desc.defines, key, nullptr, isUberShader);
	}
	else
	{
		struct FDefaultShader
		{
			const char* ShaderName;
			const char* material_lump;
			const char* mateffect_lump;
			const char* lightmodel_lump_shared;
			const char* lightmodel_lump;
			const char* Defines;
		};

		// Note: the MaterialShaderIndex enum needs to be updated whenever this array is modified.
		static const FDefaultShader defaultshaders[] =
		{
			{"Default",	            "shaders/scene/material_default.glsl",                 "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_normal.glsl",   "#define SHADERTYPE_DEFAULT\n"},
			{"Warp 1",	            "shaders/scene/material_default.glsl",                 "shaders/scene/mateffect_warp1.glsl",   "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_normal.glsl",   "#define SHADERTYPE_WARP1\n"},
			{"Warp 2",	            "shaders/scene/material_default.glsl",                 "shaders/scene/mateffect_warp2.glsl",   "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_normal.glsl",   "#define SHADERTYPE_WARP2\n"},
			{"Specular",            "shaders/scene/material_default.glsl",                 "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_specular.glsl", "#define SHADERTYPE_SPECULAR\n#define SPECULAR\n#define NORMALMAP\n"},
			{"PBR",                 "shaders/scene/material_default.glsl",                 "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_pbr.glsl",      "#define SHADERTYPE_PBR\n#define PBR\n#define NORMALMAP\n"},
			{"Paletted",	        "shaders/scene/material_paletted.glsl",                "shaders/scene/mateffect_default.glsl", nullptr,									 "shaders/scene/lightmodel_nolights.glsl", "#define SHADERTYPE_PALETTE\n#define PALETTE_EMULATION\n"},
			{"No Texture",          "shaders/scene/material_notexture.glsl",               "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_normal.glsl",   "#define SHADERTYPE_NOTEXTURE\n#define NO_LAYERS\n"},
			{"Basic Fuzz",          "shaders/scene/material_fuzz_standard.glsl",           "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_normal.glsl",   "#define SHADERTYPE_FUZZ\n#define SHADERTYPE_FUZZ_BASIC\n"},
			{"Smooth Fuzz",         "shaders/scene/material_fuzz_smooth.glsl",             "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_normal.glsl",   "#define SHADERTYPE_FUZZ\n#define SHADERTYPE_FUZZ_SMOOTH\n"},
			{"Swirly Fuzz",         "shaders/scene/material_fuzz_swirly.glsl",             "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_normal.glsl",   "#define SHADERTYPE_FUZZ\n#define SHADERTYPE_FUZZ_SWIRLY\n"},
			{"Translucent Fuzz",    "shaders/scene/material_fuzz_smoothtranslucent.glsl",  "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_normal.glsl",   "#define SHADERTYPE_FUZZ\n#define SHADERTYPE_FUZZ_TRANSLUCENT\n"},
			{"Jagged Fuzz",         "shaders/scene/material_fuzz_jagged.glsl",             "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_normal.glsl",   "#define SHADERTYPE_FUZZ\n#define SHADERTYPE_FUZZ_JAGGED\n"},
			{"Noise Fuzz",          "shaders/scene/material_fuzz_noise.glsl",              "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_normal.glsl",   "#define SHADERTYPE_FUZZ\n#define SHADERTYPE_FUZZ_NOISE\n"},
			{"Smooth Noise Fuzz",   "shaders/scene/material_fuzz_smoothnoise.glsl",        "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_normal.glsl",   "#define SHADERTYPE_FUZZ\n#define SHADERTYPE_FUZZ_SMOOTHNOISE\n"},
			{"Software Fuzz",       "shaders/scene/material_fuzz_software.glsl",           "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_shared.glsl",   "shaders/scene/lightmodel_normal.glsl",   "#define SHADERTYPE_FUZZ\n#define SHADERTYPE_FUZZ_SOFTWARE\n"},
			{nullptr,nullptr,nullptr,nullptr}
		};

		if (key.EffectState < FIRST_USER_SHADER)
		{
			const auto& desc = defaultshaders[key.EffectState];
			program->vert = LoadVertShader(desc.ShaderName, mainvp, nullptr, desc.Defines, key, nullptr, isUberShader);
			if (!key.NoFragmentShader)
				program->frag = LoadFragShader(desc.ShaderName, mainfp, desc.material_lump, desc.mateffect_lump, desc.lightmodel_lump_shared, desc.lightmodel_lump, desc.Defines, key, nullptr, isUberShader);
		}
		else
		{
			const auto& desc = usershaders[key.EffectState - FIRST_USER_SHADER];
			const FString& name = ExtractFileBase(desc.shader.GetChars());
			FString defines = defaultshaders[desc.shaderType].Defines + desc.defines;

			program->vert = LoadVertShader(name, mainvp, desc.vertshader.IsEmpty() ? nullptr : desc.vertshader.GetChars(), defines.GetChars(), key, &desc, isUberShader);
			if (!key.NoFragmentShader)
				program->frag = LoadFragShader(name, mainfp, desc.shader.GetChars(), defaultshaders[desc.shaderType].mateffect_lump, defaultshaders[desc.shaderType].lightmodel_lump_shared, defaultshaders[desc.shaderType].lightmodel_lump, defines.GetChars(), key, &desc, isUberShader);

			desc.Uniforms.WriteUniforms(program->Uniforms);
		}
	}
	return program;
}

enum class FieldCondition
{
	ALWAYS,
	NOTSIMPLE,
	HAS_CLIPDISTANCE,
	USELEVELMESH,
	GBUFFER_PASS,
	SHADE_VERTEX,
};

struct BuiltinFieldDesc : public VaryingFieldDesc
{
	FieldCondition cond;
};

static std::vector<VaryingFieldDesc> vertexShaderInputs
{
	{"aPosition",		"", UniformType::Vec4},		//0, VATTR_VERTEX
	{"aTexCoord",		"", UniformType::Vec2},		//1, VATTR_TEXCOORD
	{"aColor",			"", UniformType::Vec4},		//2, VATTR_COLOR
	{"aVertex2",		"", UniformType::Vec4},		//3, VATTR_VERTEX2
	{"aNormal",			"", UniformType::Vec4},		//4, VATTR_NORMAL
	{"aNormal2",		"", UniformType::Vec4},		//5, VATTR_NORMAL2
	{"aLightmap",		"", UniformType::Vec2},		//6, VATTR_LIGHTMAP
	{"aBoneWeight",		"", UniformType::Vec4},		//7, VATTR_BONEWEIGHT
	{"aBoneSelector",	"", UniformType::UVec4},	//8, VATTR_BONESELECTOR
	{"aDataIndex",		"", UniformType::Int},		//9, VATTR_UNIFORM_INDEXES
};

static std::vector<BuiltinFieldDesc> vertexShaderOutputs
{
	{"vTexCoord",		"",		UniformType::Vec4,	FieldCondition::ALWAYS},			//0
	{"vColor",			"",		UniformType::Vec4,	FieldCondition::ALWAYS},			//1
	{"pixelpos",		"",		UniformType::Vec4,	FieldCondition::ALWAYS},			//2
	{"glowdist",		"",		UniformType::Vec3,	FieldCondition::NOTSIMPLE},			//3
	{"gradientdist",	"",		UniformType::Vec3,	FieldCondition::NOTSIMPLE},			//4
	{"vWorldNormal",	"",		UniformType::Vec4,	FieldCondition::ALWAYS},			//5
	{"vEyeNormal",		"",		UniformType::Vec4,	FieldCondition::ALWAYS},			//6
	{"ClipDistanceA",	"",		UniformType::Vec4,	FieldCondition::HAS_CLIPDISTANCE},	//7
	{"ClipDistanceB",	"",		UniformType::Vec4,	FieldCondition::HAS_CLIPDISTANCE},	//8
	{"vLightmap",		"",		UniformType::Vec2,	FieldCondition::ALWAYS},			//9
	{"vLightmapIndex",	"flat",	UniformType::Int,	FieldCondition::ALWAYS},			//10
	{"uDataIndex",		"flat", UniformType::Int,	FieldCondition::USELEVELMESH},		//11
	{"vLightColor",		"",		UniformType::Vec3,	FieldCondition::SHADE_VERTEX},		//12
};

static std::vector<BuiltinFieldDesc> fragShaderOutputs
{
	{"FragColor",		"",		UniformType::Vec4, FieldCondition::ALWAYS},			//0
	{"FragFog",			"",		UniformType::Vec4, FieldCondition::GBUFFER_PASS},	//1
	{"FragNormal",		"",		UniformType::Vec4, FieldCondition::GBUFFER_PASS},	//2
};

static void AddVertexInFields(VulkanRenderDevice* fb, FString& layoutBlock, const VkShaderKey& key)
{
	const VkVertexFormat& vfmt = *fb->GetRenderPassManager()->GetVertexFormat(key.VertexFormat);
	for (const FVertexBufferAttribute& attr : vfmt.Attrs)
	{
		const VaryingFieldDesc& desc = vertexShaderInputs[attr.location];
		layoutBlock.AppendFormat("layout(location = %d) %s %s %s %s;\n", attr.location, desc.Property.GetChars(), "in", GetTypeStr(desc.Type), desc.Name.GetChars());
	}
}

static void AddFields(FString &layoutBlock, int &index, bool is_in, const std::vector<VaryingFieldDesc> &fields)
{
	for(auto &field : fields)
	{
		layoutBlock.AppendFormat("layout(location = %d) %s %s %s %s;\n", index, field.Property.GetChars(), is_in ? "in" : "out", GetTypeStr(field.Type), field.Name.GetChars());
		index++;
	}
}

static void AddBuiltinFields(FString &layoutBlock, int &index, bool is_in, const std::vector<BuiltinFieldDesc> &fields, const VkShaderKey& key, bool hasClipDistance)
{
	for(auto &field : fields)
	{
		switch(field.cond)
		{
		case FieldCondition::NOTSIMPLE:
			if(key.Layout.Simple) continue;
			break;
		case FieldCondition::HAS_CLIPDISTANCE:
			if(!hasClipDistance) continue;
			break;
		case FieldCondition::GBUFFER_PASS:
			if(!key.Layout.GBufferPass) continue;
			break;
		case FieldCondition::USELEVELMESH:
			if(!key.Layout.UseLevelMesh) continue;
			break;
		case FieldCondition::SHADE_VERTEX:
			if(!key.Layout.ShadeVertex) continue;
			break;
		default:
			break;
		}

		layoutBlock.AppendFormat("layout(location = %d) %s %s %s %s;\n", index, field.Property.GetChars(), is_in ? "in" : "out", GetTypeStr(field.Type), field.Name.GetChars());
		index++;
	}
}

void VkShaderManager::BuildLayoutBlock(FString &layoutBlock, bool isFrag, const VkShaderKey& key, const UserShaderDesc *shader, bool isUberShader)
{
	bool hasClipDistance = fb->GetDevice()->EnabledFeatures.Features.shaderClipDistance;

	layoutBlock << "// This must match the PushConstants struct\n";
	layoutBlock << "layout(push_constant) uniform PushConstants\n";
	layoutBlock << "{\n";
	if (key.Layout.UseLevelMesh)
	{
		layoutBlock << "    int unused0;\n";
		layoutBlock << "    int unused1;\n";
	}
	else
	{
		layoutBlock << "    int uDataIndex; // surfaceuniforms index\n";
		layoutBlock << "    int uLightIndex; // dynamic lights\n";
	}
	layoutBlock << "    int uBoneIndexBase; // bone animation\n";
	layoutBlock << "    int uFogballIndex; // fog balls\n";
	
	if(isUberShader)
	{
		layoutBlock << "    int uShaderKey1;\n";
		layoutBlock << "    int uShaderKey2;\n";
	}
	else
	{
		layoutBlock << "    int unused2;\n";
		layoutBlock << "    int unused3;\n";
	}

	layoutBlock << "    int padding0;\n";
	layoutBlock << "    int padding1;\n";

	if(shader && shader->Uniforms.UniformStructSize)
	{
		for(auto &field : shader->Uniforms.Fields)
		{
			layoutBlock.AppendFormat("    %s %s;\n", GetTypeStr(field.Type), field.Name.GetChars());
		}
	}
	layoutBlock << "};\n";

	if (!isUberShader)
	{
		layoutBlock << "    layout (constant_id = 0) const int uShaderKey1 = 0;\n";
		layoutBlock << "    layout (constant_id = 1) const int uShaderKey2 = 0;\n";
	}

	if(!isFrag)
	{
		AddVertexInFields(fb, layoutBlock, key);
	}

	{
		int index = 0;

		AddBuiltinFields(layoutBlock, index, isFrag, vertexShaderOutputs, key, hasClipDistance);

		if(shader)
		{
			AddFields(layoutBlock, index, isFrag, shader->Varyings);
		}
	}

	if(isFrag)
	{
		int index = 0;
		AddBuiltinFields(layoutBlock, index, false, fragShaderOutputs, key, hasClipDistance);
	}
}

void VkShaderManager::BuildDefinesBlock(FString &definesBlock, const char *defines, bool isFrag, const VkShaderKey& key, const UserShaderDesc *shader, bool isUberShader)
{
	if (fb->IsRayQueryEnabled())
	{
		definesBlock << "\n#define SUPPORTS_RAYQUERY\n";
	}

	definesBlock << defines;
	definesBlock << "\n#define MAX_SURFACE_UNIFORMS " << std::to_string(MAX_SURFACE_UNIFORMS).c_str() << "\n";
	definesBlock << "#define MAX_LIGHT_DATA " << std::to_string(MAX_LIGHT_DATA).c_str() << "\n";
	definesBlock << "#define MAX_FOGBALL_DATA " << std::to_string(MAX_FOGBALL_DATA).c_str() << "\n";

	if(isFrag)
	{
		definesBlock << "#define FRAGSHADER\n";
	}

	#ifdef NPOT_EMULATION
		definesBlock << "#define NPOT_EMULATION\n";
	#endif

	if (!fb->GetDevice()->EnabledFeatures.Features.shaderClipDistance)
	{
		definesBlock << "#define NO_CLIPDISTANCE_SUPPORT\n";
	}

	definesBlock << LoadPrivateShaderLump("shaders/shaderkey.glsl").GetChars() << "\n";

	if (isUberShader)
		definesBlock << "#define UBERSHADER\n";

	// Controls layout and has to be defines:

	if (key.Layout.AlphaTest) definesBlock << "#define DO_ALPHATEST\n";
	if (key.Layout.Simple) definesBlock << "#define SIMPLE\n";
	if (key.Layout.Simple3D) definesBlock << "#define SIMPLE3D\n";
	if (key.Layout.GBufferPass) definesBlock << "#define GBUFFER_PASS\n";
	if (key.Layout.UseLevelMesh) definesBlock << "#define USE_LEVELMESH\n";
	if (key.Layout.ShadeVertex) definesBlock << "#define SHADE_VERTEX\n";
	if (key.Layout.UseRaytracePrecise) definesBlock << "#define USE_RAYTRACE_PRECISE\n";

	// We could move this to shaders/shaderkey.glsl as its always the same:

	definesBlock << "#define SIMPLE2D ((uShaderKey1 & SK1_SIMPLE2D) != 0)\n";

	definesBlock << "#define TM_STENCIL (SK_GET_TEXTUREMODE() == SK1_TM_STENCIL)\n";
	definesBlock << "#define TM_OPAQUE (SK_GET_TEXTUREMODE() == SK1_TM_OPAQUE)\n";
	definesBlock << "#define TM_INVERSE (SK_GET_TEXTUREMODE() == SK1_TM_INVERSE)\n";
	definesBlock << "#define TM_ALPHATEXTURE (SK_GET_TEXTUREMODE() == SK1_TM_ALPHATEXTURE)\n";
	definesBlock << "#define TM_CLAMPY (SK_GET_TEXTUREMODE() == SK1_TM_CLAMPY)\n";
	definesBlock << "#define TM_INVERTOPAQUE (SK_GET_TEXTUREMODE() == SK1_TM_INVERTOPAQUE)\n";
	definesBlock << "#define TM_FOGLAYER (SK_GET_TEXTUREMODE() == SK1_TM_FOGLAYER)\n";

	definesBlock << "#define TEXF_ClampY ((uShaderKey1 & SK1_TEXF_CLAMPY) != 0)\n";
	definesBlock << "#define TEXF_Brightmap ((uShaderKey1 & SK1_TEXF_BRIGHTMAP) != 0)\n";
	definesBlock << "#define TEXF_Detailmap ((uShaderKey1 & SK1_TEXF_DETAILMAP) != 0)\n";
	definesBlock << "#define TEXF_Glowmap ((uShaderKey1 & SK1_TEXF_GLOWMAP) != 0)\n";

	definesBlock << "#define USE_SHADOWMAP ((uShaderKey1 & SK1_USE_SHADOWMAP) != 0)\n";

	definesBlock << "#define USE_RAYTRACE ((uShaderKey1 & SK1_USE_RAYTRACE) != 0)\n";

	definesBlock << "#define SHADOWMAP_FILTER (SK_GET_SHADOWMAP_FILTER())\n";

	definesBlock << "#define FOG_BEFORE_LIGHTS ((uShaderKey1 & SK1_FOG_BEFORE_LIGHTS) != 0)\n";
	definesBlock << "#define FOG_AFTER_LIGHTS ((uShaderKey1 & SK1_FOG_AFTER_LIGHTS) != 0)\n";
	definesBlock << "#define FOG_RADIAL ((uShaderKey1 & SK1_FOG_RADIAL) != 0)\n";

	definesBlock << "#define SWLIGHT_RADIAL ((uShaderKey1 & SK1_SWLIGHT_RADIAL) != 0)\n";
	definesBlock << "#define SWLIGHT_BANDED ((uShaderKey1 & SK1_SWLIGHT_BANDED) != 0)\n";

	definesBlock << "#define LIGHTMODE_DEFAULT (SK_GET_LIGHTMODE() == SK1_LIGHTMODE_DEFAULT)\n";
	definesBlock << "#define LIGHTMODE_SOFTWARE (SK_GET_LIGHTMODE() == SK1_LIGHTMODE_SOFTWARE)\n";
	definesBlock << "#define LIGHTMODE_VANILLA (SK_GET_LIGHTMODE() == SK1_LIGHTMODE_VANILLA)\n";
	definesBlock << "#define LIGHTMODE_BUILD (SK_GET_LIGHTMODE() == SK1_LIGHTMODE_BUILD)\n";

	definesBlock << "#define LIGHT_BLEND_CLAMPED (SK_GET_LIGHTBLENDMODE() == SK1_LIGHT_BLEND_CLAMPED)\n";
	definesBlock << "#define LIGHT_BLEND_COLORED_CLAMP (SK_GET_LIGHTBLENDMODE() == SK1_LIGHT_LIGHT_BLEND_COLORED_CLAMP)\n";
	definesBlock << "#define LIGHT_BLEND_UNCLAMPED (SK_GET_LIGHTBLENDMODE() == SK1_LIGHT_BLEND_UNCLAMPED)\n";

	definesBlock << "#define LIGHT_ATTENUATION_LINEAR ((uShaderKey1 & SK1_LIGHTATTENUATIONMODE) == 0)\n";
	definesBlock << "#define LIGHT_ATTENUATION_INVERSE_SQUARE ((uShaderKey1 & SK1_LIGHTATTENUATIONMODE) != 0)\n";

	definesBlock << "#define FOGBALLS ((uShaderKey1 & SK1_FOGBALLS) != 0)\n";
	definesBlock << "#define PALETTEMODE ((uShaderKey1 & SK1_PALETTEMODE) != 0)\n";

	definesBlock << "#define USE_DEPTHFADETHRESHOLD ((uShaderKey2 & SK2_USE_DEPTHFADETHRESHOLD) != 0)\n";
	definesBlock << "#define NOT_ALPHATEST_ONLY ((uShaderKey2 & SK2_ALPHATEST_ONLY) == 0)\n";
	
	definesBlock << "#define LIGHT_NONORMALS ((uShaderKey2 & SK2_LIGHT_NONORMALS) != 0)\n";
	definesBlock << "#define USE_SPRITE_CENTER ((uShaderKey2 & SK2_USE_SPRITECENTER) != 0)\n";

	definesBlock << "#define uFogEnabled ((uShaderKey1 & SK1_SIMPLE2D) ? -3 : 0)\n";

	// Setup fake variables for the 'in' attributes that aren't actually available because the garbage shader code thinks they exist
	// God I hate this engine... :(
	std::vector<bool> definedFields(vertexShaderInputs.size());
	bool hasNormal = false;
	const VkVertexFormat& vfmt = *fb->GetRenderPassManager()->GetVertexFormat(key.VertexFormat);
	for (const FVertexBufferAttribute& attr : vfmt.Attrs)
		definedFields[attr.location] = true;
	for (size_t i = 0; i < vertexShaderInputs.size(); i++)
	{
		if (!definedFields[i])
			definesBlock << "#define " << vertexShaderInputs[i].Name << " " << GetTypeStr(vertexShaderInputs[i].Type) << "(0)\n";
	}
}

std::vector<uint32_t> VkShaderManager::LoadVertShader(FString shadername, const char *vert_lump, const char *vert_lump_custom, const char *defines, const VkShaderKey& key, const UserShaderDesc *shader, bool isUberShader)
{
	FString definesBlock;
	BuildDefinesBlock(definesBlock, defines, false, key, shader, isUberShader);

	FString layoutBlock;
	BuildLayoutBlock(layoutBlock, false, key, shader, isUberShader);

	FString codeBlock;
	codeBlock << LoadPrivateShaderLump(vert_lump).GetChars() << "\n";
	if(vert_lump_custom)
	{
		codeBlock << "\n#line 1\n";
		codeBlock << LoadPublicShaderLump(vert_lump_custom).GetChars() << "\n";
	}
	else
	{
		codeBlock << LoadPrivateShaderLump("shaders/scene/vert_nocustom.glsl").GetChars() << "\n";
	}

	return CachedGLSLCompiler()
		.Type(ShaderType::Vertex)
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("DefinesBlock", definesBlock.GetChars())
		.AddSource("LayoutBlock", layoutBlock.GetChars())
		.AddSource("shaders/scene/layout_shared.glsl", LoadPrivateShaderLump("shaders/scene/layout_shared.glsl").GetChars())
		.AddSource(vert_lump_custom ? vert_lump_custom : vert_lump, codeBlock.GetChars())
		.Compile(fb);
}

std::vector<uint32_t> VkShaderManager::LoadFragShader(FString shadername, const char *frag_lump, const char *material_lump, const char* mateffect_lump, const char *light_lump_shared, const char *light_lump, const char *defines, const VkShaderKey& key, const UserShaderDesc *shader, bool isUberShader)
{
	FString definesBlock;
	BuildDefinesBlock(definesBlock, defines, true, key, shader, isUberShader);

	FString layoutBlock;
	BuildLayoutBlock(layoutBlock, true, key, shader, isUberShader);

	FString codeBlock;
	codeBlock << LoadPrivateShaderLump(frag_lump).GetChars() << "\n";

	FString materialname = "MaterialBlock";
	FString materialBlock;
	FString lightname = "LightBlock";
	FString lightBlock;
	FString mateffectname = "MaterialEffectBlock";
	FString mateffectBlock;

	if (material_lump)
	{
		materialname = material_lump;
		materialBlock = LoadPublicShaderLump(material_lump);

		// Attempt to fix old custom shaders:

		materialBlock = RemoveLegacyUserUniforms(materialBlock);
		materialBlock.Substitute("gl_TexCoord[0]", "vTexCoord");

		if (materialBlock.IndexOf("ProcessMaterial") < 0 && materialBlock.IndexOf("SetupMaterial") < 0)
		{
			// Old hardware shaders that implements GetTexCoord, ProcessTexel or Process

			if (materialBlock.IndexOf("GetTexCoord") >= 0)
			{
				mateffectBlock = "vec2 GetTexCoord();";
			}
			
			FString code;
			if (materialBlock.IndexOf("ProcessTexel") >= 0)
			{
				code = LoadPrivateShaderLump("shaders/scene/material_legacy_ptexel.glsl");
			}
			else if (materialBlock.IndexOf("Process") >= 0)
			{
				code = LoadPrivateShaderLump("shaders/scene/material_legacy_process.glsl");
			}
			else
			{
				code = LoadPrivateShaderLump("shaders/scene/material_default.glsl");
			}
			code << "\n#line 1\n";

			materialBlock = code + materialBlock;
		}
		else if (materialBlock.IndexOf("SetupMaterial") < 0)
		{
			// Old hardware shader implementing SetupMaterial

			definesBlock << "#define LEGACY_USER_SHADER\n";

			FString code = LoadPrivateShaderLump("shaders/scene/material_legacy_pmaterial.glsl");
			code << "\n#line 1\n";

			materialBlock = code + materialBlock;
		}
	}

	if (light_lump && lightBlock.IsEmpty())
	{
		lightname = light_lump;

		if(light_lump_shared)
		{
			lightBlock << LoadPrivateShaderLump(light_lump_shared).GetChars();
		}

		lightBlock << LoadPrivateShaderLump(light_lump).GetChars();
		
	}

	if (mateffect_lump && mateffectBlock.IsEmpty())
	{
		mateffectname = mateffect_lump;
		mateffectBlock << LoadPrivateShaderLump(mateffect_lump).GetChars();
	}

	return CachedGLSLCompiler()
		.Type(ShaderType::Fragment)
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("DefinesBlock", definesBlock.GetChars())
		.AddSource("LayoutBlock", layoutBlock.GetChars())
		.AddSource("shaders/scene/layout_shared.glsl", LoadPrivateShaderLump("shaders/scene/layout_shared.glsl").GetChars())
		.AddSource("shaders/scene/includes.glsl", LoadPrivateShaderLump("shaders/scene/includes.glsl").GetChars())
		.AddSource(mateffectname.GetChars(), mateffectBlock.GetChars())
		.AddSource(materialname.GetChars(), materialBlock.GetChars())
		.AddSource(lightname.GetChars(), lightBlock.GetChars())
		.AddSource(frag_lump, codeBlock.GetChars())
		.Compile(fb);
}

FString VkShaderManager::GetVersionBlock()
{
	FString versionBlock;

	if (fb->GetDevice()->Instance->ApiVersion >= VK_API_VERSION_1_2)
	{
		versionBlock << "#version 460 core\n";
	}
	else
	{
		versionBlock << "#version 450 core\n";
	}

	versionBlock << "#extension GL_GOOGLE_include_directive : enable\n";
	versionBlock << "#extension GL_EXT_nonuniform_qualifier : enable\r\n";

	if (fb->IsRayQueryEnabled())
	{
		versionBlock << "#extension GL_EXT_ray_query : enable\n";
	}

	return versionBlock;
}

FString VkShaderManager::LoadPublicShaderLump(const char* lumpname)
{
	return fb->GetShaderCache()->GetPublicFileText(lumpname);
}

FString VkShaderManager::LoadPrivateShaderLump(const char* lumpname)
{
	return fb->GetShaderCache()->GetPrivateFileText(lumpname);
}

VkPPShader* VkShaderManager::GetVkShader(PPShader* shader)
{
	if (!shader->Backend)
		shader->Backend = std::make_unique<VkPPShader>(fb, shader);
	return static_cast<VkPPShader*>(shader->Backend.get());
}

void VkShaderManager::AddVkPPShader(VkPPShader* shader)
{
	shader->it = PPShaders.insert(PPShaders.end(), shader);
}

void VkShaderManager::RemoveVkPPShader(VkPPShader* shader)
{
	shader->fb = nullptr;
	PPShaders.erase(shader->it);
}
