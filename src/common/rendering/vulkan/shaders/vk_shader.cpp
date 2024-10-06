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
#include "vulkan/vk_renderdevice.h"
#include <zvulkan/vulkanbuilders.h>
#include "hw_shaderpatcher.h"
#include "filesystem.h"
#include "engineerrors.h"
#include "version.h"
#include "cmdlib.h"

VkShaderManager::VkShaderManager(VulkanRenderDevice* fb) : fb(fb)
{
	ZMinMax.vert = ShaderBuilder()
		.Type(ShaderType::Vertex)
		.DebugName("ZMinMax.vert")
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("shaders/scene/vert_zminmax.glsl", LoadPrivateShaderLump("shaders/scene/vert_zminmax.glsl").GetChars())
		.Create("ZMinMax.vert", fb->GetDevice());

	ZMinMax.frag[0] = ShaderBuilder()
		.Type(ShaderType::Fragment)
		.DebugName("ZMinMax0.frag")
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("shaders/scene/frag_zminmax0.glsl", LoadPrivateShaderLump("shaders/scene/frag_zminmax0.glsl").GetChars())
		.Create("ZMinMax0.frag", fb->GetDevice());

	ZMinMax.frag[1] = ShaderBuilder()
		.Type(ShaderType::Fragment)
		.DebugName("ZMinMax0.frag")
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("DefinesBlock", "#define MULTISAMPLE\n")
		.AddSource("shaders/scene/frag_zminmax0.glsl", LoadPrivateShaderLump("shaders/scene/frag_zminmax0.glsl").GetChars())
		.Create("ZMinMax0.frag", fb->GetDevice());

	ZMinMax.frag[2] = ShaderBuilder()
		.Type(ShaderType::Fragment)
		.DebugName("ZMinMax1.frag")
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("shaders/scene/frag_zminmax1.glsl", LoadPrivateShaderLump("shaders/scene/frag_zminmax1.glsl").GetChars())
		.Create("ZMinMax1.frag", fb->GetDevice());

	LightTiles = ShaderBuilder()
		.Type(ShaderType::Compute)
		.DebugName("LightTiles.comp")
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("shaders/scene/comp_lighttiles.glsl", LoadPrivateShaderLump("shaders/scene/comp_lighttiles.glsl").GetChars())
		.Create("LightTiles.comp", fb->GetDevice());
}

VkShaderManager::~VkShaderManager()
{
}

void VkShaderManager::Deinit()
{
	while (!PPShaders.empty())
		RemoveVkPPShader(PPShaders.back());
}

VkShaderProgram* VkShaderManager::Get(const VkShaderKey& key)
{
	auto& program = programs[key];
	if (program.frag)
		return &program;

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
			const char* defines;
		};

		static const FEffectShader effectshaders[] =
		{
			{ "fogboundary",  "shaders/scene/frag_fogboundary.glsl", nullptr,                               nullptr,                                nullptr,                                "#define NO_ALPHATEST\n" },
			{ "spheremap",    "shaders/scene/frag_main.glsl",        "shaders/scene/material_default.glsl", "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_normal.glsl", "#define SPHEREMAP\n#define NO_ALPHATEST\n" },
			{ "burn",         "shaders/scene/frag_burn.glsl",        nullptr,                               nullptr,                                nullptr,                                "#define SIMPLE\n#define NO_ALPHATEST\n" },
			{ "stencil",      "shaders/scene/frag_stencil.glsl",     nullptr,                               nullptr,                                nullptr,                                "#define SIMPLE\n#define NO_ALPHATEST\n" },
			{ "portal",       "shaders/scene/frag_portal.glsl",      nullptr,                               nullptr,                                nullptr,                                "#define SIMPLE\n#define NO_ALPHATEST\n" },
			{ "dithertrans",  "shaders/scene/frag_main.glsl",        "shaders/scene/material_default.glsl", "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_normal.glsl", "#define NO_ALPHATEST\n#define DITHERTRANS\n" },
		};

		const auto& desc = effectshaders[key.SpecialEffect];
		program.vert = LoadVertShader(desc.ShaderName, mainvp, desc.defines, key.UseLevelMesh);
		if (!key.NoFragmentShader)
			program.frag = LoadFragShader(desc.ShaderName, desc.fp1, desc.fp2, desc.fp3, desc.fp4, desc.defines, key);
	}
	else
	{
		struct FDefaultShader
		{
			const char* ShaderName;
			const char* material_lump;
			const char* mateffect_lump;
			const char* lightmodel_lump;
			const char* Defines;
		};

		// Note: the MaterialShaderIndex enum needs to be updated whenever this array is modified.
		static const FDefaultShader defaultshaders[] =
		{
			{"Default",	            "shaders/scene/material_default.glsl",                 "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_normal.glsl",   ""},
			{"Warp 1",	            "shaders/scene/material_default.glsl",                 "shaders/scene/mateffect_warp1.glsl",   "shaders/scene/lightmodel_normal.glsl",   ""},
			{"Warp 2",	            "shaders/scene/material_default.glsl",                 "shaders/scene/mateffect_warp2.glsl",   "shaders/scene/lightmodel_normal.glsl",   ""},
			{"Specular",            "shaders/scene/material_spec.glsl",                    "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_specular.glsl", "#define SPECULAR\n#define NORMALMAP\n"},
			{"PBR",                 "shaders/scene/material_pbr.glsl",                     "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_pbr.glsl",      "#define PBR\n#define NORMALMAP\n"},
			{"Paletted",	        "shaders/scene/material_paletted.glsl",                "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_nolights.glsl", "#define PALETTE_EMULATION\n"},
			{"No Texture",          "shaders/scene/material_notexture.glsl",               "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_normal.glsl",   "#define NO_LAYERS\n"},
			{"Basic Fuzz",          "shaders/scene/material_fuzz_standard.glsl",           "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_normal.glsl",   ""},
			{"Smooth Fuzz",         "shaders/scene/material_fuzz_smooth.glsl",             "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_normal.glsl",   ""},
			{"Swirly Fuzz",         "shaders/scene/material_fuzz_swirly.glsl",             "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_normal.glsl",   ""},
			{"Translucent Fuzz",    "shaders/scene/material_fuzz_smoothtranslucent.glsl",  "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_normal.glsl",   ""},
			{"Jagged Fuzz",         "shaders/scene/material_fuzz_jagged.glsl",             "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_normal.glsl",   ""},
			{"Noise Fuzz",          "shaders/scene/material_fuzz_noise.glsl",              "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_normal.glsl",   ""},
			{"Smooth Noise Fuzz",   "shaders/scene/material_fuzz_smoothnoise.glsl",        "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_normal.glsl",   ""},
			{"Software Fuzz",       "shaders/scene/material_fuzz_software.glsl",           "shaders/scene/mateffect_default.glsl", "shaders/scene/lightmodel_normal.glsl",   ""},
			{nullptr,nullptr,nullptr,nullptr}
		};

		if (key.EffectState < FIRST_USER_SHADER)
		{
			const auto& desc = defaultshaders[key.EffectState];
			program.vert = LoadVertShader(desc.ShaderName, mainvp, desc.Defines, key.UseLevelMesh);
			if (!key.NoFragmentShader)
				program.frag = LoadFragShader(desc.ShaderName, mainfp, desc.material_lump, desc.mateffect_lump, desc.lightmodel_lump, desc.Defines, key);
		}
		else
		{
			const auto& desc = usershaders[key.EffectState - FIRST_USER_SHADER];
			const FString& name = ExtractFileBase(desc.shader.GetChars());
			FString defines = defaultshaders[desc.shaderType].Defines + desc.defines;

			program.vert = LoadVertShader(name, mainvp, defines.GetChars(), key.UseLevelMesh);
			if (!key.NoFragmentShader)
				program.frag = LoadFragShader(name, mainfp, desc.shader.GetChars(), defaultshaders[desc.shaderType].mateffect_lump, defaultshaders[desc.shaderType].lightmodel_lump, defines.GetChars(), key);
		}
	}
	return &program;
}

std::unique_ptr<VulkanShader> VkShaderManager::LoadVertShader(FString shadername, const char *vert_lump, const char *defines, bool levelmesh)
{
	FString definesBlock;
	definesBlock << defines;
	definesBlock << "\n#define MAX_SURFACE_UNIFORMS " << std::to_string(MAX_SURFACE_UNIFORMS).c_str() << "\n";
	definesBlock << "#define MAX_LIGHT_DATA " << std::to_string(MAX_LIGHT_DATA).c_str() << "\n";
	definesBlock << "#define MAX_FOGBALL_DATA " << std::to_string(MAX_FOGBALL_DATA).c_str() << "\n";
#ifdef NPOT_EMULATION
	definesBlock << "#define NPOT_EMULATION\n";
#endif
	if (!fb->GetDevice()->EnabledFeatures.Features.shaderClipDistance)
	{
		definesBlock << "#define NO_CLIPDISTANCE_SUPPORT\n";
	}

	if (levelmesh)
		definesBlock << "#define USE_LEVELMESH\n";

	FString layoutBlock;
	layoutBlock << LoadPrivateShaderLump("shaders/scene/layout_shared.glsl").GetChars() << "\n";
	layoutBlock << LoadPrivateShaderLump("shaders/scene/layout_vert.glsl").GetChars() << "\n";

	FString codeBlock;
	codeBlock << LoadPrivateShaderLump(vert_lump).GetChars() << "\n";

	return ShaderBuilder()
		.Type(ShaderType::Vertex)
		.DebugName(shadername.GetChars())
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("DefinesBlock", definesBlock.GetChars())
		.AddSource("LayoutBlock", layoutBlock.GetChars())
		.AddSource(vert_lump, codeBlock.GetChars())
		.OnIncludeLocal([=](std::string headerName, std::string includerName, size_t depth) { return OnInclude(headerName.c_str(), includerName.c_str(), depth, false); })
		.OnIncludeSystem([=](std::string headerName, std::string includerName, size_t depth) { return OnInclude(headerName.c_str(), includerName.c_str(), depth, true); })
		.Create(shadername.GetChars(), fb->GetDevice());
}

std::unique_ptr<VulkanShader> VkShaderManager::LoadFragShader(FString shadername, const char *frag_lump, const char *material_lump, const char* mateffect_lump, const char *light_lump, const char *defines, const VkShaderKey& key)
{
	FString definesBlock;
	if (fb->IsRayQueryEnabled()) definesBlock << "\n#define SUPPORTS_RAYQUERY\n";
	definesBlock << defines;
	definesBlock << "\n#define MAX_SURFACE_UNIFORMS " << std::to_string(MAX_SURFACE_UNIFORMS).c_str() << "\n";
	definesBlock << "#define MAX_LIGHT_DATA " << std::to_string(MAX_LIGHT_DATA).c_str() << "\n";
	definesBlock << "#define MAX_FOGBALL_DATA " << std::to_string(MAX_FOGBALL_DATA).c_str() << "\n";
#ifdef NPOT_EMULATION
	definesBlock << "#define NPOT_EMULATION\n";
#endif
	if (!fb->GetDevice()->EnabledFeatures.Features.shaderClipDistance) definesBlock << "#define NO_CLIPDISTANCE_SUPPORT\n";
	if (!key.AlphaTest) definesBlock << "#define NO_ALPHATEST\n";
	if (key.GBufferPass) definesBlock << "#define GBUFFER_PASS\n";
	if (key.AlphaTestOnly) definesBlock << "#define ALPHATEST_ONLY\n";

	switch(key.LightBlendMode)
	{
	case 0:
		definesBlock << "#define LIGHT_BLEND_CLAMPED\n";
		break;
	case 1:
		definesBlock << "#define LIGHT_BLEND_COLORED_CLAMP\n";
		break;
	case 2:
		definesBlock << "#define LIGHT_BLEND_UNCLAMPED\n";
		break;
	}

	switch(key.LightAttenuationMode)
	{
	case 0:
		definesBlock << "#define LIGHT_ATTENUATION_LINEAR\n";
		break;
	case 1:
		definesBlock << "#define LIGHT_ATTENUATION_INVERSE_SQUARE\n";
		break;
	}

	if (key.DepthFadeThreshold) definesBlock << "#define USE_DEPTHFADETHRESHOLD\n";

	if (key.Simple2D) definesBlock << "#define SIMPLE2D\n";
	if (key.ClampY) definesBlock << "#define TEXF_ClampY\n";
	if (key.Brightmap) definesBlock << "#define TEXF_Brightmap\n";
	if (key.Detailmap) definesBlock << "#define TEXF_Detailmap\n";
	if (key.Glowmap) definesBlock << "#define TEXF_Glowmap\n";

	if (key.UseRaytrace) definesBlock << "#define USE_RAYTRACE\n";
	if (key.UseShadowmap) definesBlock << "#define USE_SHADOWMAP\n";
	if (key.UseLevelMesh) definesBlock << "#define USE_LEVELMESH\n";

	switch (key.TextureMode)
	{
	case TM_STENCIL: definesBlock << "#define TM_STENCIL\n"; break;
	case TM_OPAQUE: definesBlock << "#define TM_OPAQUE\n"; break;
	case TM_INVERSE: definesBlock << "#define TM_INVERSE\n"; break;
	case TM_ALPHATEXTURE: definesBlock << "#define TM_ALPHATEXTURE\n"; break;
	case TM_CLAMPY: definesBlock << "#define TM_CLAMPY\n"; break;
	case TM_INVERTOPAQUE: definesBlock << "#define TM_INVERTOPAQUE\n"; break;
	case TM_FOGLAYER: definesBlock << "#define TM_FOGLAYER\n"; break;
	}

	switch (key.LightMode)
	{
	case 0: definesBlock << "#define LIGHTMODE_DEFAULT\n"; break;
	case 1: definesBlock << "#define LIGHTMODE_SOFTWARE\n"; break;
	case 2: definesBlock << "#define LIGHTMODE_VANILLA\n"; break;
	case 3: definesBlock << "#define LIGHTMODE_BUILD\n"; break;
	}

	if (key.FogBeforeLights) definesBlock << "#define FOG_BEFORE_LIGHTS\n";
	if (key.FogAfterLights) definesBlock << "#define FOG_AFTER_LIGHTS\n";
	if (key.FogRadial) definesBlock << "#define FOG_RADIAL\n";
	if (key.SWLightRadial) definesBlock << "#define SWLIGHT_RADIAL\n";
	if (key.SWLightBanded) definesBlock << "#define SWLIGHT_BANDED\n";
	if (key.FogBalls) definesBlock << "#define FOGBALLS\n";

	// Backwards compatibility with shaders that rape and pillage... uhm, I mean abused uFogEnabled to detect 2D rendering!
	if (key.Simple2D) definesBlock << "#define uFogEnabled -3\n";
	else definesBlock << "#define uFogEnabled 0\n";

	FString layoutBlock;
	layoutBlock << LoadPrivateShaderLump("shaders/scene/layout_shared.glsl").GetChars() << "\n";
	layoutBlock << LoadPrivateShaderLump("shaders/scene/layout_frag.glsl").GetChars() << "\n";

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
		lightBlock << LoadPrivateShaderLump(light_lump).GetChars();
	}

	if (mateffect_lump && mateffectBlock.IsEmpty())
	{
		mateffectname = mateffect_lump;
		mateffectBlock << LoadPrivateShaderLump(mateffect_lump).GetChars();
	}

	return ShaderBuilder()
		.Type(ShaderType::Fragment)
		.DebugName(shadername.GetChars())
		.AddSource("VersionBlock", GetVersionBlock().GetChars())
		.AddSource("DefinesBlock", definesBlock.GetChars())
		.AddSource("LayoutBlock", layoutBlock.GetChars())
		.AddSource("shaders/scene/includes.glsl", LoadPrivateShaderLump("shaders/scene/includes.glsl").GetChars())
		.AddSource(mateffectname.GetChars(), mateffectBlock.GetChars())
		.AddSource(materialname.GetChars(), materialBlock.GetChars())
		.AddSource(lightname.GetChars(), lightBlock.GetChars())
		.AddSource(frag_lump, codeBlock.GetChars())
		.OnIncludeLocal([=](std::string headerName, std::string includerName, size_t depth) { return OnInclude(headerName.c_str(), includerName.c_str(), depth, false); })
		.OnIncludeSystem([=](std::string headerName, std::string includerName, size_t depth) { return OnInclude(headerName.c_str(), includerName.c_str(), depth, true); })
		.Create(shadername.GetChars(), fb->GetDevice());
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

ShaderIncludeResult VkShaderManager::OnInclude(FString headerName, FString includerName, size_t depth, bool system)
{
	if (depth > 8)
		I_Error("Too much include recursion!");

	FString includeguardname;
	includeguardname << "_HEADERGUARD_" << headerName.GetChars();
	includeguardname.ReplaceChars("/\\.", '_');

	FString code;
	code << "#ifndef " << includeguardname.GetChars() << "\n";
	code << "#define " << includeguardname.GetChars() << "\n";
	code << "#line 1\n";

	if (system)
		code << LoadPrivateShaderLump(headerName.GetChars()).GetChars() << "\n";
	else
		code << LoadPublicShaderLump(headerName.GetChars()).GetChars() << "\n";

	code << "#endif\n";

	return ShaderIncludeResult(headerName.GetChars(), code.GetChars());
}

FString VkShaderManager::LoadPublicShaderLump(const char *lumpname)
{
	int lump = fileSystem.CheckNumForFullName(lumpname, 0);
	if (lump == -1) lump = fileSystem.CheckNumForFullName(lumpname);
	if (lump == -1) I_Error("Unable to load '%s'", lumpname);
	return GetStringFromLump(lump);
}

FString VkShaderManager::LoadPrivateShaderLump(const char *lumpname)
{
	int lump = fileSystem.CheckNumForFullName(lumpname, 0);
	if (lump == -1) I_Error("Unable to load '%s'", lumpname);
	return GetStringFromLump(lump);
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
	shader->Reset();
	shader->fb = nullptr;
	PPShaders.erase(shader->it);
}
