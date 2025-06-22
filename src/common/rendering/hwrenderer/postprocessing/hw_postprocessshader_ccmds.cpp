/*
**  Debug ccmds for post-process shaders
**  Copyright (c) 2022 Rachael Alexanderson
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
*/

#include "hwrenderer/postprocessing/hw_postprocessshader.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"
#include "printf.h"
#include "c_dispatch.h"

CCMD (shaderenable)
{
	if (argv.argc() < 3)
	{
		Printf("Usage: shaderenable [name] [1/0/-1]\nState '-1' toggles the active shader state\n");
		return;
	}
	auto shaderName = argv[1];

	int value = atoi(argv[2]);

	bool found = 0;
	for (unsigned int i = 0; i < PostProcessShaders.Size(); i++)
	{
		PostProcessShader &shader = PostProcessShaders[i];
		if (shader.Name.Compare(shaderName) == 0)
		{
			if (value != -1)
				shader.Enabled = value;
			else
				shader.Enabled = !shader.Enabled; //toggle
			found = 1;
		}
	}
	if (found && value != -1)
		Printf("Changed active state of all instances of %s to %s\n", shaderName, value?"On":"Off");
	else if (found)
		Printf("Toggled active state of all instances of %s\n", shaderName);
	else
		Printf("No shader named '%s' found\n", shaderName);
}

static void PrintUniform(const char * shaderName, const char * uniformName, const UniformField &uniform)
{
	float * f = (float *)uniform.Value;
	int * i = (int *)uniform.Value;
	switch(uniform.Type)
	{
	default:
	case UniformType::Undefined:
		Printf("Shader '%s': could not find uniform '%s'\n", shaderName, uniformName);
		break;
	case UniformType::Int:
		Printf("Shader '%s' uniform '%s': %d\n", shaderName, uniformName, *i);
		break;
	case UniformType::Float:
		Printf("Shader '%s' uniform '%s': %f\n", shaderName, uniformName, *f);
		break;
	case UniformType::Vec2:
		Printf("Shader '%s' uniform '%s': %f %f\n", shaderName, uniformName, (double)f[0], (double)f[1]);
		break;
	case UniformType::Vec3:
		Printf("Shader '%s' uniform '%s': %f %f %f\n", shaderName, uniformName, (double)f[0], (double)f[1], (double)f[2]);
		break;
	case UniformType::Vec4:
		Printf("Shader '%s' uniform '%s': %f %f %f %f\n", shaderName, uniformName, (double)f[0], (double)f[1], (double)f[2], (double)f[3]);
		break;
	}
}

CCMD (shaderuniform)
{
	if (argv.argc() < 3)
	{
		Printf("Usage: shaderuniform [shader name] [uniform name] [[value1 ..]]\n");
		return;
	}
	auto shaderName = argv[1];
	auto uniformName = argv[2];

	bool found = 0;
	for (unsigned int i = 0; i < PostProcessShaders.Size(); i++)
	{
		PostProcessShader &shader = PostProcessShaders[i];
		if (shader.Name.Compare(shaderName) == 0)
		{
			if (argv.argc() > 3)
			{
				auto uniform = shader.Uniforms.GetField(uniformName);
				float * f = (float *)uniform.Value;
				int * i = (int *)uniform.Value;
				switch(uniform.Type)
				{
				default:
				case UniformType::Undefined:
					Printf("Shader '%s': could not find uniform '%s'\n", shaderName, uniformName);
					break;
				case UniformType::Int:
					*i = atoi(argv[3]);
					break;
				case UniformType::Float:
					*f = atof(argv[3]);
					break;
				case UniformType::Vec2:
					f[0] = argv.argc()>=4 ? atof(argv[3]) : 0.0;
					f[1] = argv.argc()>=5 ? atof(argv[4]) : 0.0;
					break;
				case UniformType::Vec3:
					f[0] = argv.argc()>=4 ? atof(argv[3]) : 0.0;
					f[1] = argv.argc()>=5 ? atof(argv[4]) : 0.0;
					f[2] = argv.argc()>=6 ? atof(argv[5]) : 0.0;
					break;
				case UniformType::Vec4:
					f[0] = argv.argc()>=4 ? atof(argv[3]) : 0.0;
					f[1] = argv.argc()>=5 ? atof(argv[4]) : 0.0;
					f[2] = argv.argc()>=6 ? atof(argv[5]) : 0.0;
					f[3] = argv.argc()>=7 ? atof(argv[6]) : 1.0;
					break;
				}
			}
			else
			{
				PrintUniform(shaderName, uniformName, shader.Uniforms.GetField(uniformName));
			}
			found = 1;
		}
	}
	if (found && argv.argc() > 3)
		Printf("Changed uniforms of %s named %s\n", shaderName, uniformName);
	else if (!found)
		Printf("No shader named '%s' found\n", shaderName);
}

CCMD(listshaders)
{
	for (unsigned int i = 0; i < PostProcessShaders.Size(); i++)
	{
		PostProcessShader &shader = PostProcessShaders[i];
		Printf("Shader (%i): %s\n", i, shader.Name.GetChars());
	}
}

CCMD(listuniforms)
{
	if (argv.argc() < 2)
	{
		Printf("Usage: listuniforms [name]\n");
		return;
	}
	auto shaderName = argv[1];

	bool found = 0;
	for (unsigned int i = 0; i < PostProcessShaders.Size(); i++)
	{
		PostProcessShader &shader = PostProcessShaders[i];
		if (shader.Name.Compare(shaderName) == 0)
		{
			Printf("Shader '%s' uniforms:\n", shaderName);

			for(auto &field : shader.Uniforms.Fields)
			{
				PrintUniform(shaderName, field.Name.GetChars(), shader.Uniforms.GetField(field.Name));
			}

			found = 1;
		}
	}
	if (!found)
		Printf("No shader named '%s' found\n", shaderName);
}
