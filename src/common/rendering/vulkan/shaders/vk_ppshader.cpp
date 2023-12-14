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

#include "vk_ppshader.h"
#include "vk_shader.h"
#include "vulkan/vk_renderdevice.h"
#include "vulkan/commands/vk_commandbuffer.h"
#include <zvulkan/vulkanbuilders.h>
#include "filesystem.h"
#include "cmdlib.h"

VkPPShader::VkPPShader(VulkanRenderDevice* fb, PPShader *shader) : fb(fb)
{
	FString prolog;
	if (!shader->Uniforms.empty())
		prolog = CreateUniformBlockDecl("Uniforms", shader->Uniforms, -1);
	prolog += shader->Defines;

	VertexShader = ShaderBuilder()
		.Type(ShaderType::Vertex)
		.AddSource(shader->VertexShader.GetChars(), LoadShaderCode(shader->VertexShader, "", shader->Version).GetChars())
		.OnIncludeLocal([=](std::string headerName, std::string includerName, size_t depth) { return OnInclude(headerName.c_str(), includerName.c_str(), depth, false); })
		.OnIncludeSystem([=](std::string headerName, std::string includerName, size_t depth) { return OnInclude(headerName.c_str(), includerName.c_str(), depth, true); })
		.DebugName(shader->VertexShader.GetChars())
		.Create(shader->VertexShader.GetChars(), fb->GetDevice());

	FragmentShader = ShaderBuilder()
		.Type(ShaderType::Fragment)
		.AddSource(shader->FragmentShader.GetChars(), LoadShaderCode(shader->FragmentShader, prolog, shader->Version).GetChars())
		.OnIncludeLocal([=](std::string headerName, std::string includerName, size_t depth) { return OnInclude(headerName.c_str(), includerName.c_str(), depth, false); })
		.OnIncludeSystem([=](std::string headerName, std::string includerName, size_t depth) { return OnInclude(headerName.c_str(), includerName.c_str(), depth, true); })
		.DebugName(shader->FragmentShader.GetChars())
		.Create(shader->FragmentShader.GetChars(), fb->GetDevice());

	fb->GetShaderManager()->AddVkPPShader(this);
}

VkPPShader::~VkPPShader()
{
	if (fb)
		fb->GetShaderManager()->RemoveVkPPShader(this);
}

void VkPPShader::Reset()
{
	if (fb)
	{
		fb->GetCommands()->DrawDeleteList->Add(std::move(VertexShader));
		fb->GetCommands()->DrawDeleteList->Add(std::move(FragmentShader));
	}
}

ShaderIncludeResult VkPPShader::OnInclude(FString headerName, FString includerName, size_t depth, bool system)
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

FString VkPPShader::LoadPublicShaderLump(const char* lumpname)
{
	int lump = fileSystem.CheckNumForFullName(lumpname, 0);
	if (lump == -1) lump = fileSystem.CheckNumForFullName(lumpname);
	if (lump == -1) I_Error("Unable to load '%s'", lumpname);
	return GetStringFromLump(lump);
}

FString VkPPShader::LoadPrivateShaderLump(const char* lumpname)
{
	int lump = fileSystem.CheckNumForFullName(lumpname, 0);
	if (lump == -1) I_Error("Unable to load '%s'", lumpname);
	return GetStringFromLump(lump);
}

FString VkPPShader::LoadShaderCode(const FString &lumpName, const FString &defines, int version)
{
	int lump = fileSystem.CheckNumForFullName(lumpName.GetChars());
	if (lump == -1) I_FatalError("Unable to load '%s'", lumpName.GetChars());
	auto sp = fileSystem.ReadFile(lump);
	FString code = GetStringFromLump(lump);

	FString patchedCode;
	patchedCode.AppendFormat("#version %d core\n", fb->GetDevice()->Instance->ApiVersion >= VK_API_VERSION_1_2 ? 460 : 450);
	patchedCode << "#extension GL_GOOGLE_include_directive : enable\n";
	patchedCode << defines;
	patchedCode << "#line 1\n";
	patchedCode << code;
	return patchedCode;
}

FString VkPPShader::CreateUniformBlockDecl(const char* name, const std::vector<UniformFieldDesc>& fields, int bindingpoint)
{
	FString decl;
	FString layout;
	if (bindingpoint == -1)
	{
		layout = "push_constant";
	}
	else
	{
		layout.Format("std140, binding = %d", bindingpoint);
	}
	decl.Format("layout(%s) uniform %s\n{\n", layout.GetChars(), name);
	for (size_t i = 0; i < fields.size(); i++)
	{
		decl.AppendFormat("\t%s %s;\n", GetTypeStr(fields[i].Type), fields[i].Name);
	}
	decl += "};\n";

	return decl;
}

const char* VkPPShader::GetTypeStr(UniformType type)
{
	switch (type)
	{
	default:
	case UniformType::Int: return "int";
	case UniformType::UInt: return "uint";
	case UniformType::Float: return "float";
	case UniformType::Vec2: return "vec2";
	case UniformType::Vec3: return "vec3";
	case UniformType::Vec4: return "vec4";
	case UniformType::IVec2: return "ivec2";
	case UniformType::IVec3: return "ivec3";
	case UniformType::IVec4: return "ivec4";
	case UniformType::UVec2: return "uvec2";
	case UniformType::UVec3: return "uvec3";
	case UniformType::UVec4: return "uvec4";
	case UniformType::Mat4: return "mat4";
	}
}
