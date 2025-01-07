/*
** gldefs.cpp
** GLDEFS parser
**
**---------------------------------------------------------------------------
** Copyright 2003 Timothy Stump
** Copyright 2005-2018 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/
#include <ctype.h>

#include "sc_man.h"

#include "filesystem.h"
#include "gi.h"
#include "r_state.h"
#include "stats.h"
#include "v_text.h"
#include "g_levellocals.h"
#include "a_dynlight.h"
#include "v_video.h"
#include "skyboxtexture.h"
#include "hwrenderer/postprocessing/hw_postprocessshader.h"
#include "hw_material.h"
#include "texturemanager.h"
#include "gameconfigfile.h"
#include "m_argv.h"

void AddLightDefaults(FLightDefaults *defaults, double attnFactor);
void AddLightAssociation(const char *actor, const char *frame, const char *light);
void InitializeActorLights(TArray<FLightAssociation> &LightAssociations);
void ParseColorization(FScanner& sc);

extern TDeletingArray<FLightDefaults *> LightDefaults;
extern int AttenuationIsSet;

GlobalShaderDesc globalshaders[NUM_BUILTIN_SHADERS];

struct ExtraUniformCVARData
{
	FString Shader;
	FString Uniform;
	void * data = nullptr;
	ExtraUniformCVARData* Next = nullptr;
	void (*OldCallback)(FBaseCVar &);
};

void pp_uniform_get_data(ExtraUniformCVARData* data)
{
	if (!(data->data))
	{
		for (unsigned int i = 0; i < PostProcessShaders.Size(); i++)
		{
			PostProcessShader& shader = PostProcessShaders[i];
			if (strcmp(shader.Name.GetChars(), data->Shader.GetChars()) == 0)
			{
				data->data = shader.Uniforms.GetField(data->Uniform).Value;
			}
		}
	}
}

static void do_pp_uniform_set1i(int value, ExtraUniformCVARData* data)
{
	pp_uniform_get_data(data);

	int * i1 = (int *)data->data;
	if (i1)
	{
		i1[0] = value;
	}

	if (data->Next)
		do_pp_uniform_set1i(value, data->Next);
}

static void do_pp_uniform_set1f(float value, ExtraUniformCVARData* data)
{
	pp_uniform_get_data(data);

	float * f1 = (float *)data->data;
	if (f1)
	{
		f1[0] = value;
	}

	if (data->Next)
		do_pp_uniform_set1f(value, data->Next);
}

static void do_pp_uniform_set2f(float x, float y, ExtraUniformCVARData* data)
{
	pp_uniform_get_data(data);

	float * f2 = (float *)data->data;
	if (f2)
	{
		f2[0] = x;
		f2[1] = y;
	}

	if (data->Next)
		do_pp_uniform_set2f(x, y, data->Next);
}

static void do_pp_uniform_set3f(float x, float y, float z, ExtraUniformCVARData* data)
{
	pp_uniform_get_data(data);

	float * f3 = (float *)data->data;
	if (f3)
	{
		f3[0] = x;
		f3[1] = y;
		f3[2] = z;
	}

	if (data->Next)
		do_pp_uniform_set3f(x, y, z, data->Next);
}

static void do_pp_uniform_set4f(float x, float y, float z, float w, ExtraUniformCVARData* data)
{
	pp_uniform_get_data(data);

	float * f4 = (float *)data->data;
	if (f4)
	{
		f4[0] = x;
		f4[1] = y;
		f4[2] = z;
		f4[3] = w;
	}

	if (data->Next)
		do_pp_uniform_set4f(x, y, z, w, data->Next);
}

template<typename T>
void pp_uniform_callback1i(T &self)
{
	auto data = (ExtraUniformCVARData*)self.GetExtraDataPointer2();
	if(data->OldCallback) data->OldCallback(self);

	do_pp_uniform_set1i(*self, data);
}

template<typename T>
void pp_uniform_callback1f(T &self)
{
	auto data = (ExtraUniformCVARData*)self.GetExtraDataPointer2();
	if(data->OldCallback) data->OldCallback(self);

	do_pp_uniform_set1f(*self, data);
}

void pp_uniform_callback_color3f(FColorCVar &self)
{
	auto data = (ExtraUniformCVARData*)self.GetExtraDataPointer2();
	if(data->OldCallback) data->OldCallback(self);

	PalEntry col;
	col.d = *self;

	do_pp_uniform_set3f(col.r / 255.0, col.g / 255.0, col.b / 255.0, data);
}

void pp_uniform_callback_color4f(FColorCVar &self)
{
	auto data = (ExtraUniformCVARData*)self.GetExtraDataPointer2();
	if(data->OldCallback) data->OldCallback(self);

	PalEntry col;
	col.d = *self;

	do_pp_uniform_set4f(col.r / 255.0, col.g / 255.0, col.b / 255.0, col.a / 255.0, data);
}

//-----------------------------------------------------------------------------
//
// ParseVavoomSkybox
//
//-----------------------------------------------------------------------------

static void ParseVavoomSkybox()
{
	int lump = fileSystem.CheckNumForName("SKYBOXES");

	if (lump < 0) return;

	FScanner sc(lump);
	while (sc.GetString())
	{
		int facecount=0;
		int maplump = -1;
		bool error = false;
		FString s = sc.String;
		FSkyBox * sb = new FSkyBox(sc.String);
		sb->fliptop = true;
		sc.MustGetStringName("{");
		while (!sc.CheckString("}"))
		{
			if (facecount<6) 
			{
				sc.MustGetStringName("{");
				sc.MustGetStringName("map");
				sc.MustGetString();

				maplump = fileSystem.CheckNumForFullName(sc.String, true);

				auto tex = TexMan.FindGameTexture(sc.String, ETextureType::Wall, FTextureManager::TEXMAN_TryAny);
				if (tex == NULL)
				{
					sc.ScriptMessage("Texture '%s' not found in Vavoom skybox '%s'\n", sc.String, s.GetChars());
					error = true;
				}
				sb->faces[facecount] = tex;
				sc.MustGetStringName("}");
			}
			facecount++;
		}
		if (facecount != 6)
		{
			sc.ScriptError("%s: Skybox definition requires 6 faces", s.GetChars());
		}
		sb->SetSize();
		if (!error)
		{
			TexMan.AddGameTexture(MakeGameTexture(sb, s.GetChars(), ETextureType::Override));
		}
	}
}



//==========================================================================
//
// light definition keywords
//
//==========================================================================


static const char *LightTags[]=
{
   "color",
   "size",
   "secondarySize",
   "offset",
   "chance",
   "interval",
   "scale",
   "frame",
   "light",
   "{",
   "}",
   "subtractive",
   "additive",
   "halo",
   "dontlightself",
   "attenuate",
   "dontlightactors",
   "spot",
   "noshadowmap",
   "dontlightothers",
   "dontlightmap",
   "trace",
   "shadowminquality",
   nullptr
};


enum {
   LIGHTTAG_COLOR,
   LIGHTTAG_SIZE,
   LIGHTTAG_SECSIZE,
   LIGHTTAG_OFFSET,
   LIGHTTAG_CHANCE,
   LIGHTTAG_INTERVAL,
   LIGHTTAG_SCALE,
   LIGHTTAG_FRAME,
   LIGHTTAG_LIGHT,
   LIGHTTAG_OPENBRACE,
   LIGHTTAG_CLOSEBRACE,
   LIGHTTAG_SUBTRACTIVE,
   LIGHTTAG_ADDITIVE,
   LIGHTTAG_HALO,
   LIGHTTAG_DONTLIGHTSELF,
   LIGHTTAG_ATTENUATE,
   LIGHTTAG_DONTLIGHTACTORS,
   LIGHTTAG_SPOT,
   LIGHTTAG_NOSHADOWMAP,
   LIGHTTAG_DONTLIGHTOTHERS,
   LIGHTTAG_DONTLIGHTMAP,
   LIGHTTAG_TRACE,
   LIGHTTAG_SHADOW_MINQUALITY,
};

//==========================================================================
//
// Top level keywords
//
//==========================================================================

// these are the core types available in the *DEFS lump
static const char *CoreKeywords[]=
{
   "pointlight",
   "pulselight",
   "flickerlight",
   "flickerlight2",
   "sectorlight",
   "object",
   "clearlights",
   "shader",
   "clearshaders",
   "skybox",
   "glow",
   "brightmap",
   "disable_fullbright",
   "hardwareshader",
   "detail",
   "#include",
   "material",
   "lightsizefactor",
   "colorization",
   nullptr
};


enum
{
   LIGHT_POINT,
   LIGHT_PULSE,
   LIGHT_FLICKER,
   LIGHT_FLICKER2,
   LIGHT_SECTOR,
   LIGHT_OBJECT,
   LIGHT_CLEAR,
   TAG_SHADER,
   TAG_CLEARSHADERS,
   TAG_SKYBOX,
   TAG_GLOW,
   TAG_BRIGHTMAP,
   TAG_DISABLE_FB,
   TAG_HARDWARESHADER,
   TAG_DETAIL,
   TAG_INCLUDE,
   TAG_MATERIAL,
   TAG_LIGHTSIZEFACTOR,
   TAG_COLORIZATION,
};

//==========================================================================
//
// GLDEFS parser
//
//==========================================================================

class GLDefsParser
{
	FScanner sc;
	int workingLump;
	int ScriptDepth = 0;
	TArray<FLightAssociation> &LightAssociations;
	double lightSizeFactor = 1.;

	//==========================================================================
	//
	// This is only here so any shader definition for ZDoomGL can be skipped
	// There is no functionality for this stuff!
	//
	//==========================================================================

	bool ParseShader()
	{
		int  ShaderDepth = 0;

		if (sc.GetString())
		{
			char *tmp;

			tmp = strstr(sc.String, "{");
			while (tmp)
			{
				ShaderDepth++;
				tmp++;
				tmp = strstr(tmp, "{");
			}

			tmp = strstr(sc.String, "}");
			while (tmp)
			{
				ShaderDepth--;
				tmp++;
				tmp = strstr(tmp, "}");
			}

			if (ShaderDepth == 0) return true;
		}
		return false;
	}

	//==========================================================================
	//
	// Parses a GLBoom+ detail texture definition
	//
	// Syntax is this:
	//	detail
	//	{
	//		(walls | flats) [default_detail_name [width [height [offset_x [offset_y]]]]]
	//		{
	//			texture_name [detail_name [width [height [offset_x [offset_y]]]]]
	//		}
	//	}
	// This merely parses the block and returns no error if valid. The feature
	// is not actually implemented, so nothing else happens.
	//
	// The semantics of this are too horrible to comprehend (default detail texture???)
	// so if this ever gets done, the parser will look different.
	//==========================================================================

	void ParseDetailTexture()
	{
		while (!sc.CheckToken('}'))
		{
			sc.MustGetString();
			if (sc.Compare("walls") || sc.Compare("flats"))
			{
				if (!sc.CheckToken('{'))
				{
					sc.MustGetString();  // Default detail texture
					if (sc.CheckFloat()) // Width
					if (sc.CheckFloat()) // Height
					if (sc.CheckFloat()) // OffsX
					if (sc.CheckFloat()) // OffsY
					{
						// Nothing
					}
				}
				else sc.UnGet();
				sc.MustGetToken('{');
				while (!sc.CheckToken('}'))
				{
					sc.MustGetString();  // Texture
					if (sc.GetString())	 // Detail texture
					{
						if (sc.CheckFloat()) // Width
						if (sc.CheckFloat()) // Height
						if (sc.CheckFloat()) // OffsX
						if (sc.CheckFloat()) // OffsY
						{
							// Nothing
						}
					}
					else sc.UnGet();
				}
			}
		}
	}

	
	//==========================================================================
	//
	//
	//
	//==========================================================================

	float ParseFloat(FScanner &sc)
	{
	   sc.MustGetFloat();
	   return float(sc.Float);
	}


	int ParseInt(FScanner &sc)
	{
	   sc.MustGetNumber();
	   return sc.Number;
	}


	char *ParseString(FScanner &sc)
	{
	   sc.GetString();
	   return sc.String;
	}


	void ParseTriple(FScanner &sc, float floatVal[3])
	{
	   for (int i = 0; i < 3; i++)
	   {
		  floatVal[i] = ParseFloat(sc);
	   }
	}


	//-----------------------------------------------------------------------------
	//
	//
	//
	//-----------------------------------------------------------------------------

	void AddLightAssociation(const char *actor, const char *frame, const char *light)
	{
		FLightAssociation *temp;
		unsigned int i;
		FLightAssociation assoc(actor, frame, light);

		for (i = 0; i < LightAssociations.Size(); i++)
		{
			temp = &LightAssociations[i];
			if (temp->ActorName() == assoc.ActorName())
			{
				if (strcmp(temp->FrameName(), assoc.FrameName()) == 0)
				{
					temp->ReplaceLightName(assoc.Light());
					return;
				}
			}
		}

		LightAssociations.Push(assoc);
	}

	//-----------------------------------------------------------------------------
	//
	// Note: The different light type parsers really could use some consolidation...
	//
	//-----------------------------------------------------------------------------

	void ParsePointLight()
	{
		int type;
		float floatTriple[3];
		int intVal;
		FLightDefaults *defaults;

		// get name
		sc.GetString();
		FName name = sc.String;

		// check for opening brace
		sc.GetString();
		if (sc.Compare("{"))
		{
			defaults = new FLightDefaults(name, PointLight);
			ScriptDepth++;
			while (ScriptDepth)
			{
				sc.GetString();
				type = sc.MatchString(LightTags);
				switch (type)
				{
				case LIGHTTAG_OPENBRACE:
					ScriptDepth++;
					break;
				case LIGHTTAG_CLOSEBRACE:
					ScriptDepth--;
					break;
				case LIGHTTAG_COLOR:
					ParseTriple(sc, floatTriple);
					defaults->SetArg(LIGHT_RED, clamp<int>((int)(floatTriple[0] * 255), 0, 255));
					defaults->SetArg(LIGHT_GREEN, clamp<int>((int)(floatTriple[1] * 255), 0, 255));
					defaults->SetArg(LIGHT_BLUE, clamp<int>((int)(floatTriple[2] * 255), 0, 255));
					break;
				case LIGHTTAG_OFFSET:
					ParseTriple(sc, floatTriple);
					defaults->SetOffset(floatTriple);
					break;
				case LIGHTTAG_SIZE:
					intVal = clamp<int>(ParseInt(sc), 1, 1024);
					defaults->SetArg(LIGHT_INTENSITY, intVal);
					break;
				case LIGHTTAG_SUBTRACTIVE:
					defaults->SetSubtractive(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_ADDITIVE:
					defaults->SetAdditive(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_HALO:	// old development garbage
					ParseInt(sc);
					break;
				case LIGHTTAG_NOSHADOWMAP:
					defaults->SetNoShadowmap(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTSELF:
					defaults->SetDontLightSelf(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_ATTENUATE:
					defaults->SetAttenuate(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTACTORS:
					defaults->SetDontLightActors(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTOTHERS:
					defaults->SetDontLightOthers(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTMAP:
					defaults->SetDontLightMap(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_TRACE:
					defaults->SetTrace(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_SHADOW_MINQUALITY:
					defaults->SetShadowMinQuality(ParseInt(sc));
					break;
				case LIGHTTAG_SPOT:
					{
						float innerAngle = ParseFloat(sc);
						float outerAngle = ParseFloat(sc);
						defaults->SetSpot(true);
						defaults->SetSpotInnerAngle(innerAngle);
						defaults->SetSpotOuterAngle(outerAngle);
					}
					break;
				default:
					sc.ScriptError("Unknown tag: %s\n", sc.String);
				}
			}
			AddLightDefaults(defaults, lightSizeFactor);
		}
		else
		{
			sc.ScriptError("Expected '{'.\n");
		}
	}

	//-----------------------------------------------------------------------------
	//
	//
	//
	//-----------------------------------------------------------------------------

	void ParsePulseLight()
	{
		int type;
		float floatVal, floatTriple[3];
		int intVal;
		FLightDefaults *defaults;

		// get name
		sc.GetString();
		FName name = sc.String;

		// check for opening brace
		sc.GetString();
		if (sc.Compare("{"))
		{
			defaults = new FLightDefaults(name, PulseLight);
			ScriptDepth++;
			while (ScriptDepth)
			{
				sc.GetString();
				type = sc.MatchString(LightTags);
				switch (type)
				{
				case LIGHTTAG_OPENBRACE:
					ScriptDepth++;
					break;
				case LIGHTTAG_CLOSEBRACE:
					ScriptDepth--;
					break;
				case LIGHTTAG_COLOR:
					ParseTriple(sc, floatTriple);
					defaults->SetArg(LIGHT_RED, clamp<int>((int)(floatTriple[0] * 255), 0, 255));
					defaults->SetArg(LIGHT_GREEN, clamp<int>((int)(floatTriple[1] * 255), 0, 255));
					defaults->SetArg(LIGHT_BLUE, clamp<int>((int)(floatTriple[2] * 255), 0, 255));
					break;
				case LIGHTTAG_OFFSET:
					ParseTriple(sc, floatTriple);
					defaults->SetOffset(floatTriple);
					break;
				case LIGHTTAG_SIZE:
					intVal = clamp<int>(ParseInt(sc), 1, 1024);
					defaults->SetArg(LIGHT_INTENSITY, intVal);
					break;
				case LIGHTTAG_SECSIZE:
					intVal = clamp<int>(ParseInt(sc), 1, 1024);
					defaults->SetArg(LIGHT_SECONDARY_INTENSITY, intVal);
					break;
				case LIGHTTAG_INTERVAL:
					floatVal = ParseFloat(sc);
					defaults->SetParameter(floatVal * TICRATE);
					break;
				case LIGHTTAG_SUBTRACTIVE:
					defaults->SetSubtractive(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_HALO:	// old development garbage
					ParseInt(sc);
					break;
				case LIGHTTAG_NOSHADOWMAP:
					defaults->SetNoShadowmap(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTSELF:
					defaults->SetDontLightSelf(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_ATTENUATE:
					defaults->SetAttenuate(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTACTORS:
					defaults->SetDontLightActors(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTOTHERS:
					defaults->SetDontLightOthers(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTMAP:
					defaults->SetDontLightMap(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_TRACE:
					defaults->SetTrace(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_SHADOW_MINQUALITY:
					defaults->SetShadowMinQuality(ParseInt(sc));
					break;
				case LIGHTTAG_SPOT:
					{
						float innerAngle = ParseFloat(sc);
						float outerAngle = ParseFloat(sc);
						defaults->SetSpot(true);
						defaults->SetSpotInnerAngle(innerAngle);
						defaults->SetSpotOuterAngle(outerAngle);
					}
					break;
				default:
					sc.ScriptError("Unknown tag: %s\n", sc.String);
				}
			}
			defaults->OrderIntensities();

			AddLightDefaults(defaults, lightSizeFactor);
		}
		else
		{
			sc.ScriptError("Expected '{'.\n");
		}
	}


	//-----------------------------------------------------------------------------
	//
	//
	//
	//-----------------------------------------------------------------------------

	void ParseFlickerLight()
	{
		int type;
		float floatVal, floatTriple[3];
		int intVal;
		FLightDefaults *defaults;

		// get name
		sc.GetString();
		FName name = sc.String;

		// check for opening brace
		sc.GetString();
		if (sc.Compare("{"))
		{
			defaults = new FLightDefaults(name, FlickerLight);
			ScriptDepth++;
			while (ScriptDepth)
			{
				sc.GetString();
				type = sc.MatchString(LightTags);
				switch (type)
				{
				case LIGHTTAG_OPENBRACE:
					ScriptDepth++;
					break;
				case LIGHTTAG_CLOSEBRACE:
					ScriptDepth--;
					break;
				case LIGHTTAG_COLOR:
					ParseTriple(sc, floatTriple);
					defaults->SetArg(LIGHT_RED, clamp<int>((int)(floatTriple[0] * 255), 0, 255));
					defaults->SetArg(LIGHT_GREEN, clamp<int>((int)(floatTriple[1] * 255), 0, 255));
					defaults->SetArg(LIGHT_BLUE, clamp<int>((int)(floatTriple[2] * 255), 0, 255));
					break;
				case LIGHTTAG_OFFSET:
					ParseTriple(sc, floatTriple);
					defaults->SetOffset(floatTriple);
					break;
				case LIGHTTAG_SIZE:
					intVal = clamp<int>(ParseInt(sc), 1, 1024);
					defaults->SetArg(LIGHT_INTENSITY, intVal);
					break;
				case LIGHTTAG_SECSIZE:
					intVal = clamp<int>(ParseInt(sc), 1, 1024);
					defaults->SetArg(LIGHT_SECONDARY_INTENSITY, intVal);
					break;
				case LIGHTTAG_CHANCE:
					floatVal = ParseFloat(sc);
					defaults->SetParameter(floatVal*360.);
					break;
				case LIGHTTAG_SUBTRACTIVE:
					defaults->SetSubtractive(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_HALO:	// old development garbage
					ParseInt(sc);
					break;
				case LIGHTTAG_NOSHADOWMAP:
					defaults->SetNoShadowmap(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTSELF:
					defaults->SetDontLightSelf(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_ATTENUATE:
					defaults->SetAttenuate(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTACTORS:
					defaults->SetDontLightActors(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTOTHERS:
					defaults->SetDontLightOthers(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTMAP:
					defaults->SetDontLightMap(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_TRACE:
					defaults->SetTrace(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_SHADOW_MINQUALITY:
					defaults->SetShadowMinQuality(ParseInt(sc));
					break;
				case LIGHTTAG_SPOT:
					{
						float innerAngle = ParseFloat(sc);
						float outerAngle = ParseFloat(sc);
						defaults->SetSpot(true);
						defaults->SetSpotInnerAngle(innerAngle);
						defaults->SetSpotOuterAngle(outerAngle);
					}
					break;
				default:
					sc.ScriptError("Unknown tag: %s\n", sc.String);
				}
			}
			defaults->OrderIntensities();
			AddLightDefaults(defaults, lightSizeFactor);
		}
		else
		{
			sc.ScriptError("Expected '{'.\n");
		}
	}


	//-----------------------------------------------------------------------------
	//
	//
	//
	//-----------------------------------------------------------------------------

	void ParseFlickerLight2()
	{
		int type;
		float floatVal, floatTriple[3];
		int intVal;
		FLightDefaults *defaults;

		// get name
		sc.GetString();
		FName name = sc.String;

		// check for opening brace
		sc.GetString();
		if (sc.Compare("{"))
		{
			defaults = new FLightDefaults(name, RandomFlickerLight);
			ScriptDepth++;
			while (ScriptDepth)
			{
				sc.GetString();
				type = sc.MatchString(LightTags);
				switch (type)
				{
				case LIGHTTAG_OPENBRACE:
					ScriptDepth++;
					break;
				case LIGHTTAG_CLOSEBRACE:
					ScriptDepth--;
					break;
				case LIGHTTAG_COLOR:
					ParseTriple(sc, floatTriple);
					defaults->SetArg(LIGHT_RED, clamp<int>((int)(floatTriple[0] * 255), 0, 255));
					defaults->SetArg(LIGHT_GREEN, clamp<int>((int)(floatTriple[1] * 255), 0, 255));
					defaults->SetArg(LIGHT_BLUE, clamp<int>((int)(floatTriple[2] * 255), 0, 255));
					break;
				case LIGHTTAG_OFFSET:
					ParseTriple(sc, floatTriple);
					defaults->SetOffset(floatTriple);
					break;
				case LIGHTTAG_SIZE:
					intVal = clamp<int>(ParseInt(sc), 1, 1024);
					defaults->SetArg(LIGHT_INTENSITY, intVal);
					break;
				case LIGHTTAG_SECSIZE:
					intVal = clamp<int>(ParseInt(sc), 1, 1024);
					defaults->SetArg(LIGHT_SECONDARY_INTENSITY, intVal);
					break;
				case LIGHTTAG_INTERVAL:
					floatVal = ParseFloat(sc);
					defaults->SetParameter(floatVal * 360.);
					break;
				case LIGHTTAG_SUBTRACTIVE:
					defaults->SetSubtractive(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_HALO:	// old development garbage
					ParseInt(sc);
					break;
				case LIGHTTAG_NOSHADOWMAP:
					defaults->SetNoShadowmap(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTSELF:
					defaults->SetDontLightSelf(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_ATTENUATE:
					defaults->SetAttenuate(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTACTORS:
					defaults->SetDontLightActors(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTOTHERS:
					defaults->SetDontLightOthers(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTMAP:
					defaults->SetDontLightMap(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_TRACE:
					defaults->SetTrace(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_SHADOW_MINQUALITY:
					defaults->SetShadowMinQuality(ParseInt(sc));
					break;
				case LIGHTTAG_SPOT:
					{
						float innerAngle = ParseFloat(sc);
						float outerAngle = ParseFloat(sc);
						defaults->SetSpot(true);
						defaults->SetSpotInnerAngle(innerAngle);
						defaults->SetSpotOuterAngle(outerAngle);
					}
					break;
				default:
					sc.ScriptError("Unknown tag: %s\n", sc.String);
				}
			}
			if (defaults->GetArg(LIGHT_SECONDARY_INTENSITY) < defaults->GetArg(LIGHT_INTENSITY))
			{
				int v = defaults->GetArg(LIGHT_SECONDARY_INTENSITY);
				defaults->SetArg(LIGHT_SECONDARY_INTENSITY, defaults->GetArg(LIGHT_INTENSITY));
				defaults->SetArg(LIGHT_INTENSITY, v);
			}
			AddLightDefaults(defaults, lightSizeFactor);
		}
		else
		{
			sc.ScriptError("Expected '{'.\n");
		}
	}


	//-----------------------------------------------------------------------------
	//
	//
	//
	//-----------------------------------------------------------------------------

	void ParseSectorLight()
	{
		int type;
		float floatVal;
		float floatTriple[3];
		FLightDefaults *defaults;

		// get name
		sc.GetString();
		FName name = sc.String;

		// check for opening brace
		sc.GetString();
		if (sc.Compare("{"))
		{
			defaults = new FLightDefaults(name, SectorLight);
			ScriptDepth++;
			while (ScriptDepth)
			{
				sc.GetString();
				type = sc.MatchString(LightTags);
				switch (type)
				{
				case LIGHTTAG_OPENBRACE:
					ScriptDepth++;
					break;
				case LIGHTTAG_CLOSEBRACE:
					ScriptDepth--;
					break;
				case LIGHTTAG_COLOR:
					ParseTriple(sc, floatTriple);
					defaults->SetArg(LIGHT_RED, clamp<int>((int)(floatTriple[0] * 255), 0, 255));
					defaults->SetArg(LIGHT_GREEN, clamp<int>((int)(floatTriple[1] * 255), 0, 255));
					defaults->SetArg(LIGHT_BLUE, clamp<int>((int)(floatTriple[2] * 255), 0, 255));
					break;
				case LIGHTTAG_OFFSET:
					ParseTriple(sc, floatTriple);
					defaults->SetOffset(floatTriple);
					break;
				case LIGHTTAG_SCALE:
					floatVal = ParseFloat(sc);
					defaults->SetArg(LIGHT_INTENSITY, clamp((int)(floatVal * 255), 1, 1024));
					break;
				case LIGHTTAG_SUBTRACTIVE:
					defaults->SetSubtractive(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_HALO:	// old development garbage
					ParseInt(sc);
					break;
				case LIGHTTAG_NOSHADOWMAP:
					defaults->SetNoShadowmap(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTSELF:
					defaults->SetDontLightSelf(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_ATTENUATE:
					defaults->SetAttenuate(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTACTORS:
					defaults->SetDontLightActors(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTOTHERS:
					defaults->SetDontLightOthers(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_DONTLIGHTMAP:
					defaults->SetDontLightMap(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_TRACE:
					defaults->SetTrace(ParseInt(sc) != 0);
					break;
				case LIGHTTAG_SHADOW_MINQUALITY:
					defaults->SetShadowMinQuality(ParseInt(sc));
					break;
				case LIGHTTAG_SPOT:
					{
						float innerAngle = ParseFloat(sc);
						float outerAngle = ParseFloat(sc);
						defaults->SetSpot(true);
						defaults->SetSpotInnerAngle(innerAngle);
						defaults->SetSpotOuterAngle(outerAngle);
					}
					break;
				default:
					sc.ScriptError("Unknown tag: %s\n", sc.String);
				}
			}
			AddLightDefaults(defaults, lightSizeFactor);
		}
		else
		{
			sc.ScriptError("Expected '{'.\n");
		}
	}

	//-----------------------------------------------------------------------------
	//
	//
	//
	//-----------------------------------------------------------------------------

	void ParseFrame(const FString &name)
	{
		int type, startDepth;
		FString frameName;

		// get name
		sc.GetString();
		if (strlen(sc.String) > 8)
		{
			sc.ScriptError("Name longer than 8 characters: %s\n", sc.String);
		}
		frameName = sc.String;
		frameName.ToUpper();

		startDepth = ScriptDepth;

		// check for opening brace
		sc.GetString();
		if (sc.Compare("{"))
		{
			ScriptDepth++;
			while (ScriptDepth > startDepth)
			{
				sc.GetString();
				type = sc.MatchString(LightTags);
				switch (type)
				{
				case LIGHTTAG_OPENBRACE:
					ScriptDepth++;
					break;
				case LIGHTTAG_CLOSEBRACE:
					ScriptDepth--;
					break;
				case LIGHTTAG_LIGHT:
					ParseString(sc);
					AddLightAssociation(name.GetChars(), frameName.GetChars(), sc.String);
					break;
				default:
					sc.ScriptError("Unknown tag: %s\n", sc.String);
				}
			}
		}
		else
		{
			sc.ScriptError("Expected '{'.\n");
		}
	}

	//-----------------------------------------------------------------------------
	//
	//
	//
	//-----------------------------------------------------------------------------

	void ParseObject()
	{
		int type;
		FString name;

		// get name
		sc.GetString();
		name = sc.String;
		if (!PClass::FindActor(name))
			sc.ScriptMessage("Warning: dynamic lights attached to non-existent actor %s\n", name.GetChars());

		// check for opening brace
		sc.GetString();
		if (sc.Compare("{"))
		{
			ScriptDepth++;
			while (ScriptDepth)
			{
				sc.GetString();
				type = sc.MatchString(LightTags);
				switch (type)
				{
				case LIGHTTAG_OPENBRACE:
					ScriptDepth++;
					break;
				case LIGHTTAG_CLOSEBRACE:
					ScriptDepth--;
					break;
				case LIGHTTAG_FRAME:
					ParseFrame(name);
					break;
				default:
					sc.ScriptError("Unknown tag: %s\n", sc.String);
				}
			}
		}
		else
		{
			sc.ScriptError("Expected '{'.\n");
		}
	}
	

	//-----------------------------------------------------------------------------
	//
	//
	//
	//-----------------------------------------------------------------------------

	void ParseGldefSkybox()
	{
		int facecount=0;

		sc.MustGetString();

		FString s = sc.String;
		FSkyBox * sb = new FSkyBox(s.GetChars());
		if (sc.CheckString("fliptop"))
		{
			sb->fliptop = true;
		}
		sc.MustGetStringName("{");
		while (!sc.CheckString("}"))
		{
			sc.MustGetString();
			if (facecount<6) 
			{
				sb->faces[facecount] = TexMan.GetGameTexture(TexMan.GetTextureID(sc.String, ETextureType::Wall, FTextureManager::TEXMAN_TryAny|FTextureManager::TEXMAN_Overridable));
			}
			facecount++;
		}
		if (facecount != 3 && facecount != 6)
		{
			sc.ScriptError("%s: Skybox definition requires either 3 or 6 faces", s.GetChars());
		}
		sb->SetSize();
		TexMan.AddGameTexture(MakeGameTexture(sb, s.GetChars(), ETextureType::Override));
	}

	//===========================================================================
	// 
	//	Reads glow definitions from GLDEFS
	//
	//===========================================================================

	void ParseGlow()
	{
		sc.MustGetStringName("{");
		while (!sc.CheckString("}"))
		{
			sc.MustGetString();
			if (sc.Compare("FLATS"))
			{
				sc.MustGetStringName("{");
				while (!sc.CheckString("}"))
				{
					sc.MustGetString();
					FTextureID flump=TexMan.CheckForTexture(sc.String, ETextureType::Flat,FTextureManager::TEXMAN_TryAny);
					auto tex = TexMan.GetGameTexture(flump);
					if (tex) tex->SetAutoGlowing();
				}
			}
			else if (sc.Compare("WALLS"))
			{
				sc.MustGetStringName("{");
				while (!sc.CheckString("}"))
				{
					sc.MustGetString();
					FTextureID flump=TexMan.CheckForTexture(sc.String, ETextureType::Wall,FTextureManager::TEXMAN_TryAny);
					auto tex = TexMan.GetGameTexture(flump);
					if (tex) tex->SetAutoGlowing();
				}
			}
			else if (sc.Compare("TEXTURE"))
			{
				sc.SetCMode(true);
				sc.MustGetString();
				FTextureID flump=TexMan.CheckForTexture(sc.String, ETextureType::Flat,FTextureManager::TEXMAN_TryAny);
				auto tex = TexMan.GetGameTexture(flump);
				sc.MustGetStringName(",");
				sc.MustGetString();
				PalEntry color = V_GetColor(sc.String);
				//sc.MustGetStringName(",");
				//sc.MustGetNumber();
				if (sc.CheckString(","))
				{
					if (sc.CheckNumber())
					{
						if (tex) tex->SetGlowHeight(sc.Number);
						if (!sc.CheckString(",")) goto skip_fb;
					}

					sc.MustGetStringName("fullbright");
					if (tex) tex->SetFullbright();
				}
			skip_fb:
				sc.SetCMode(false);

				if (tex && color != 0)
				{
					tex->SetGlowing(color);
				}
			}
		}
	}


	//==========================================================================
	//
	// Parses a brightmap definition
	//
	//==========================================================================

	void ParseBrightmap()
	{
		ETextureType type = ETextureType::Any;
		bool disable_fullbright=false;
		bool thiswad = false;
		bool iwad = false;
		FGameTexture *bmtex = NULL;

		sc.MustGetString();
		if (sc.Compare("texture")) type = ETextureType::Wall;
		else if (sc.Compare("flat")) type = ETextureType::Flat;
		else if (sc.Compare("sprite")) type = ETextureType::Sprite;
		else sc.UnGet();

		sc.MustGetString();
		FTextureID no = TexMan.CheckForTexture(sc.String, type, FTextureManager::TEXMAN_TryAny | FTextureManager::TEXMAN_Overridable);
		auto tex = TexMan.GetGameTexture(no);

		sc.MustGetToken('{');
		while (!sc.CheckToken('}'))
		{
			sc.MustGetString();
			if (sc.Compare("disablefullbright"))
			{
				// This can also be used without a brightness map to disable
				// fullbright in rotations that only use brightness maps on
				// other angles.
				disable_fullbright = true;
			}
			else if (sc.Compare("thiswad"))
			{
				// only affects textures defined in the WAD containing the definition file.
				thiswad = true;
			}
			else if (sc.Compare ("iwad"))
			{
				// only affects textures defined in the IWAD.
				iwad = true;
			}
			else if (sc.Compare ("map"))
			{
				sc.MustGetString();

				if (bmtex != NULL)
				{
					Printf("Multiple brightmap definitions in texture %s\n", tex? tex->GetName().GetChars() : "(null)");
				}

				bmtex = TexMan.FindGameTexture(sc.String, ETextureType::Any, FTextureManager::TEXMAN_TryAny);

				if (bmtex == NULL) 
					Printf("Brightmap '%s' not found in texture '%s'\n", sc.String, tex? tex->GetName().GetChars() : "(null)");
			}
		}
		if (!tex)
		{
			return;
		}
		if (thiswad || iwad)
		{
			bool useme = false;
			int lumpnum = tex->GetSourceLump();

			if (lumpnum != -1)
			{
				if (iwad && fileSystem.GetFileContainer(lumpnum) <= fileSystem.GetMaxIwadNum()) useme = true;
				if (thiswad && fileSystem.GetFileContainer(lumpnum) == fileSystem.GetFileContainer(workingLump)) useme = true;
			}
			if (!useme) return;
		}

		if (bmtex != NULL)
		{
			tex->SetBrightmap(bmtex);
		}	
		tex->SetDisableFullbright(disable_fullbright);
	}

	void SetShaderIndex(FGameTexture *tex, unsigned index)
	{
		auto desc = usershaders[index - FIRST_USER_SHADER];
		if (desc.disablealphatest)
		{
			tex->SetTranslucent(true);
		}
		tex->SetShaderIndex(index);
	}

	//==========================================================================
	//
	// Parses a material definition
	//
	//==========================================================================

	void ParseMaterial()
	{
		ETextureType type = ETextureType::Any;
		bool disable_fullbright = false;
		bool disable_fullbright_specified = false;
		bool thiswad = false;
		bool iwad = false;
		bool no_mipmap = false;

		bool is_globalshader = false;
		TArray<int> globaltargets;
		FString str_globaltargets;

		UserShaderDesc usershader;
		TArray<FString> texNameList;
		TArray<int> texNameIndex;
		float speed = 1.f;

		MaterialLayers mlay = { -1000, -1000 };
		FGameTexture* textures[6] = {};
		const char *keywords[7] = { "brightmap", "normal", "specular", "metallic", "roughness", "ao", nullptr };
		const char *notFound[6] = { "Brightmap", "Normalmap", "Specular texture", "Metallic texture", "Roughness texture", "Ambient occlusion texture" };
		
		FGameTexture* tex = nullptr;

		sc.MustGetString();
		if(sc.Compare("globalshader"))
		{
			is_globalshader = true;
			usershader.shaderFlags |= SFlag_Global; // make sure global usershader objects aren't reused for material shaders

			sc.MustGetString();

			if (sc.Compare("all"))
			{
				str_globaltargets = "all";
				globaltargets.Push(SHADER_Default);
				globaltargets.Push(SHADER_Warp1);
				globaltargets.Push(SHADER_Warp2);
				globaltargets.Push(SHADER_Specular);
				globaltargets.Push(SHADER_PBR);
				globaltargets.Push(SHADER_Paletted);
				globaltargets.Push(SHADER_NoTexture);
				globaltargets.Push(SHADER_BasicFuzz);
				globaltargets.Push(SHADER_SmoothFuzz);
				globaltargets.Push(SHADER_SwirlyFuzz);
				globaltargets.Push(SHADER_TranslucentFuzz);
				globaltargets.Push(SHADER_JaggedFuzz);
				globaltargets.Push(SHADER_NoiseFuzz);
				globaltargets.Push(SHADER_SmoothNoiseFuzz);
				globaltargets.Push(SHADER_SoftwareFuzz);
			}
			else if(sc.Compare("default"))
			{
				str_globaltargets = "default";
				globaltargets.Push(SHADER_Default);
			}
			else if(sc.Compare("defaultwarp"))
			{
				str_globaltargets = "defaultwarp";
				globaltargets.Push(SHADER_Default);
				globaltargets.Push(SHADER_Warp1);
				globaltargets.Push(SHADER_Warp2);
			}
			else if(sc.Compare("warp"))
			{
				str_globaltargets = "warp";
				globaltargets.Push(SHADER_Warp1);
				globaltargets.Push(SHADER_Warp2);
			}
			else if(sc.Compare("specular"))
			{
				str_globaltargets = "specular";
				globaltargets.Push(SHADER_Specular);
			}
			else if(sc.Compare("pbr"))
			{
				str_globaltargets = "pbr";
				globaltargets.Push(SHADER_PBR);
			}
			else if(sc.Compare("paletted"))
			{
				str_globaltargets = "paletted";
				globaltargets.Push(SHADER_Paletted);
			}
			else if(sc.Compare("notexture"))
			{
				str_globaltargets = "notexture";
				globaltargets.Push(SHADER_NoTexture);
			}
			else if(sc.Compare("nonfuzz"))
			{
				str_globaltargets = "nonfuzz";
				globaltargets.Push(SHADER_Default);
				globaltargets.Push(SHADER_Warp1);
				globaltargets.Push(SHADER_Warp2);
				globaltargets.Push(SHADER_Specular);
				globaltargets.Push(SHADER_PBR);
				globaltargets.Push(SHADER_Paletted);
			}
			else if(sc.Compare("fuzz"))
			{
				str_globaltargets = "fuzz";
				globaltargets.Push(SHADER_BasicFuzz);
				globaltargets.Push(SHADER_SmoothFuzz);
				globaltargets.Push(SHADER_SwirlyFuzz);
				globaltargets.Push(SHADER_TranslucentFuzz);
				globaltargets.Push(SHADER_JaggedFuzz);
				globaltargets.Push(SHADER_NoiseFuzz);
				globaltargets.Push(SHADER_SmoothNoiseFuzz);
				globaltargets.Push(SHADER_SoftwareFuzz);
			}
			else
			{
				sc.ScriptMessage("Invalid globalshader target\n", sc.String);
			}
		}
		else
		{
			if (sc.Compare("texture")) type = ETextureType::Wall;
			else if (sc.Compare("flat")) type = ETextureType::Flat;
			else if (sc.Compare("sprite")) type = ETextureType::Sprite;
			else sc.UnGet();

			sc.MustGetString();
			FTextureID no = TexMan.CheckForTexture(sc.String, type, FTextureManager::TEXMAN_TryAny | FTextureManager::TEXMAN_Overridable);
			tex = TexMan.GetGameTexture(no);

			if (tex == nullptr)
			{
				sc.ScriptMessage("Material definition refers nonexistent texture '%s'\n", sc.String);
			}
			else tex->AddAutoMaterials();	// We need these before setting up the texture.
		}

		bool do_strict = gl_strict_gldefs || is_globalshader;

		sc.MustGetToken('{');
		while (!sc.CheckToken('}'))
		{
			bool isProperty = false;

			sc.MustGetString();

			if(!is_globalshader)
			{
				if (sc.Compare("disablefullbright"))
				{
					isProperty = true;
					// This can also be used without a brightness map to disable
					// fullbright in rotations that only use brightness maps on
					// other angles.
					disable_fullbright = true;
					disable_fullbright_specified = true;
				}
				else if (sc.Compare("thiswad"))
				{
					isProperty = true;
					// only affects textures defined in the WAD containing the definition file.
					thiswad = true;
				}
				else if (sc.Compare ("iwad"))
				{
					// only affects textures defined in the IWAD.
					iwad = true;
					isProperty = true;
				}
				else if (sc.Compare("nomipmap"))
				{
					isProperty = true;
					no_mipmap = true;
				}
				else if (sc.Compare("glossiness"))
				{
					isProperty = true;
					sc.MustGetFloat();
					mlay.Glossiness = (float)sc.Float;
				}
				else if (sc.Compare("specularlevel"))
				{
					isProperty = true;
					sc.MustGetFloat();
					mlay.SpecularLevel = (float)sc.Float;
				}
				else if (sc.Compare("depthfadethreshold"))
				{
					isProperty = true;
					sc.MustGetFloat();
					tex->DepthFadeThreshold = (float)sc.Float;
				}
				else if (sc.Compare("speed"))
				{
					isProperty = true;
					sc.MustGetFloat();
					speed = float(sc.Float);
				}
				else if (sc.Compare("disablealphatest"))
				{
					isProperty = true;
					tex->SetTranslucent(true);
					if (usershader.shader.IsNotEmpty())
						usershader.disablealphatest = true;
				}
			}
			if(!isProperty)
			{
				if (sc.Compare("shader"))
				{
					isProperty = true;
					sc.MustGetString();
					usershader.shader = sc.String;
				}
				if (sc.Compare("vertshader"))
				{
					isProperty = true;
					sc.MustGetString();
					usershader.vertshader = sc.String;
				}
				else if (sc.Compare("texture"))
				{
					isProperty = true;
					sc.MustGetString();
					FString textureName = sc.String;
					for (FString &texName : texNameList)
					{
						if (!texName.Compare(textureName))
						{
							if(is_globalshader)
							{
								sc.ScriptError("Trying to redefine custom hardware shader texture '%s' in global shader '%s'\n", textureName.GetChars(), str_globaltargets.GetChars());
							}
							else
							{
								sc.ScriptError("Trying to redefine custom hardware shader texture '%s' in texture '%s'\n", textureName.GetChars(), tex ? tex->GetName().GetChars() : "(null)");
							}
						}
					}
					sc.MustGetString();
					if (tex || is_globalshader)
					{
						bool okay = false;
						size_t texIndex = 0;
						for (size_t i = 0; i < countof(mlay.CustomShaderTextures); i++)
						{
							if (!mlay.CustomShaderTextures[i])
							{
								mlay.CustomShaderTextureSampling[texIndex] = MaterialLayerSampling::Default;
								mlay.CustomShaderTextures[i] = TexMan.FindGameTexture(sc.String, ETextureType::Any, FTextureManager::TEXMAN_TryAny);
								if (!mlay.CustomShaderTextures[i])
								{
									if(is_globalshader)
									{
										sc.ScriptError("Custom hardware shader texture '%s' not found in global shader '%s'\n", sc.String, str_globaltargets.GetChars());
									}
									else
									{
										sc.ScriptError("Custom hardware shader texture '%s' not found in texture '%s'\n", sc.String, tex->GetName().GetChars());
									}
								}

								texNameList.Push(textureName);
								texNameIndex.Push((int)i);
								texIndex = i;
								okay = true;
								break;
							}
						}
						if (!okay)
						{
							if(is_globalshader)
							{
								sc.ScriptError("Error: out of texture units in global shader '%s'\n", str_globaltargets.GetChars());
							}
							else
							{
								sc.ScriptError("Error: out of texture units in texture '%s'", tex->GetName().GetChars());
							}
						}

						if (sc.CheckToken('{'))
						{
							while (!sc.CheckToken('}'))
							{
								sc.MustGetString();
								if (sc.Compare("filter"))
								{
									sc.MustGetString();
									if (sc.Compare("nearest"))
									{
										if(okay)
											mlay.CustomShaderTextureSampling[texIndex] = MaterialLayerSampling::NearestMipLinear;
									}
									else if (sc.Compare("linear"))
									{
										if (okay)
											mlay.CustomShaderTextureSampling[texIndex] = MaterialLayerSampling::LinearMipLinear;
									}
									else if (sc.Compare("default"))
									{
										if (okay)
											mlay.CustomShaderTextureSampling[texIndex] = MaterialLayerSampling::Default;
									}
									else
									{
										if(is_globalshader)
										{
											sc.ScriptError("Error: unexpected '%s' when reading filter property in global shader '%s'\n", sc.String, str_globaltargets.GetChars());
										}
										else
										{
											sc.ScriptError("Error: unexpected '%s' when reading filter property in texture '%s'\n", sc.String, tex ? tex->GetName().GetChars() : "(null)");
										}
									}
								}
							}
						}
					}
				}
				else if (sc.Compare("define"))
				{
					isProperty = true;
					sc.MustGetString();
					FString defineName = sc.String;
					FString defineValue = "";
					if (sc.CheckToken('='))
					{
						sc.MustGetString();
						defineValue = sc.String;
					}
					usershader.defines.AppendFormat("#define %s %s\n", defineName.GetChars(), defineValue.GetChars());
				}
				else if(!is_globalshader)
				{
					for (int i = 0; keywords[i] != nullptr; i++)
					{
						if (sc.Compare (keywords[i]))
						{
							isProperty = true;
							sc.MustGetString();
							if (textures[i])
								Printf("Multiple %s definitions in texture %s\n", keywords[i], tex? tex->GetName().GetChars() : "(null)");
							textures[i] = TexMan.FindGameTexture(sc.String, ETextureType::Any, FTextureManager::TEXMAN_TryAny);
							if (!textures[i])
								Printf("%s '%s' not found in texture '%s'\n", notFound[i], sc.String, tex? tex->GetName().GetChars() : "(null)");
							break;
						}
					}
				}
			}
			if(!isProperty && do_strict)
			{
				if(is_globalshader)
				{
					sc.ScriptError("Unknown keyword '%s' in global shader '%s'", sc.String, str_globaltargets.GetChars());
				}
				else
				{
					sc.ScriptError("Unknown keyword '%s' in texture '%s'", sc.String, tex? tex->GetName().GetChars() : "(null)");
				}
			}
		}
		if (!tex && !is_globalshader)
		{
			return;
		}
		if(is_globalshader)
		{
			if(fileSystem.GetFileContainer(workingLump) > fileSystem.GetMaxIwadNum())
			{
				sc.ScriptError("globalshader only supported on iwad");
				return;
			}
			else for(int target : globaltargets)
			{
				if(globalshaders[target].shaderindex >= 0)
				{
					sc.ScriptError("globalshader already exists for '%s'", str_globaltargets.GetChars());
					return;
				}
			}

			if (usershader.shader.IsNotEmpty())
			{
				int lump = fileSystem.CheckNumForFullName(usershader.shader.GetChars());
				if (lump == -1)
				{
					sc.ScriptError("inexistent shader lump '%s' in globalshader '%s'", usershader.shader.GetChars(), str_globaltargets.GetChars());
					return;
				}

				if (usershader.vertshader.IsNotEmpty())
				{
					int lump = fileSystem.CheckNumForFullName(usershader.vertshader.GetChars());
					if (lump == -1)
					{
						sc.ScriptError("inexistent vertex shader lump '%s' in globalshader '%s'", usershader.vertshader.GetChars(), str_globaltargets.GetChars());
						return;
					}
				}

				for(int target : globaltargets)
				{
					FString baseDefines = usershader.defines;

					int firstUserTexture;

					if (target == SHADER_Specular)
					{
						firstUserTexture = 7;
					}
					else if (target == SHADER_PBR)
					{
						firstUserTexture = 9;
					}
					else
					{
						firstUserTexture = 5;
					}

					for (unsigned int i = 0; i < texNameList.Size(); i++)
					{
						usershader.defines.AppendFormat("#define %s texture%d\n", texNameList[i].GetChars(), texNameIndex[i] + firstUserTexture);
					}

					usershader.shaderType = MaterialShaderIndex(target);

					globalshaders[target].shaderindex = usershaders.Push(usershader) + FIRST_USER_SHADER;

					for (int i = 0; i < MAX_CUSTOM_HW_SHADER_TEXTURES; i++)
					{
						if (mlay.CustomShaderTextures[i])
						{
							globalshaders[target].CustomShaderTextureSampling[i] = mlay.CustomShaderTextureSampling[i];
							globalshaders[target].CustomShaderTextures[i] = mlay.CustomShaderTextures[i]->GetTexture();
						}
					}

					usershader.defines = baseDefines;
				}
			}
			else
			{
				sc.ScriptError("shader lump not specified for global shader '%s'", usershader.shader.GetChars(), str_globaltargets.GetChars());
			}
		}
		else 
		{
			if (thiswad || iwad)
			{
				bool useme = false;
				int lumpnum = tex->GetSourceLump();

				if (lumpnum != -1)
				{
					if (iwad && fileSystem.GetFileContainer(lumpnum) <= fileSystem.GetMaxIwadNum()) useme = true;
					if (thiswad && fileSystem.GetFileContainer(lumpnum) == fileSystem.GetFileContainer(workingLump)) useme = true;
				}
				if (!useme) return;
			}

			tex->SetNoMipmap(no_mipmap);

			FGameTexture **bindings[6] =
			{
				&mlay.Brightmap,
				&mlay.Normal,
				&mlay.Specular,
				&mlay.Metallic,
				&mlay.Roughness,
				&mlay.AmbientOcclusion
			};
			for (int i = 0; keywords[i] != nullptr; i++)
			{
				if (textures[i])
				{
					*bindings[i] = textures[i];
				}
			}

			if (disable_fullbright_specified)
				tex->SetDisableFullbright(disable_fullbright);

			if (usershader.shader.IsNotEmpty())
			{
				if(do_strict)
				{
					int lump = fileSystem.CheckNumForFullName(usershader.shader.GetChars());
					if (lump == -1)
					{
						sc.ScriptError("inexistent shader lump '%s' in globalshader '%s'", usershader.shader.GetChars(), str_globaltargets.GetChars());
						return;
					}
				}

				if (usershader.vertshader.IsNotEmpty())
				{
					int lump = fileSystem.CheckNumForFullName(usershader.vertshader.GetChars());
					if (lump == -1)
					{
						sc.ScriptError("inexistent vertex shader lump '%s' in globalshader '%s'", usershader.vertshader.GetChars(), str_globaltargets.GetChars());
						return;
					}
				}

				int firstUserTexture;
				if ((mlay.Normal || tex->GetNormalmap()) && (mlay.Specular || tex->GetSpecularmap()))
				{
					usershader.shaderType = SHADER_Specular;
					firstUserTexture = 7;
				}
				else if ((mlay.Normal || tex->GetNormalmap()) && (mlay.Metallic || tex->GetMetallic()) && (mlay.Roughness || tex->GetRoughness()) && (mlay.AmbientOcclusion || tex->GetAmbientOcclusion()))
				{
					usershader.shaderType = SHADER_PBR;
					firstUserTexture = 9;
				}
				else
				{
					usershader.shaderType = SHADER_Default;
					firstUserTexture = 5;
				}

				for (unsigned int i = 0; i < texNameList.Size(); i++)
				{
					usershader.defines.AppendFormat("#define %s texture%d\n", texNameList[i].GetChars(), texNameIndex[i] + firstUserTexture);
				}

				if (tex->isWarped() != 0)
				{
					Printf("Cannot combine warping with hardware shader on texture '%s'\n", tex->GetName().GetChars());
					return;
				}
				tex->SetShaderSpeed(speed);

				for (unsigned i = 0; i < usershaders.Size(); i++)
				{
					if (!usershaders[i].shader.CompareNoCase(usershader.shader) &&
						usershaders[i].shaderType == usershader.shaderType &&
						usershaders[i].shaderFlags == usershader.shaderFlags &&
						!usershaders[i].defines.Compare(usershader.defines))
					{
						SetShaderIndex(tex, i + FIRST_USER_SHADER);
						tex->SetShaderLayers(mlay);
						return;
					}
				}

				SetShaderIndex(tex, usershaders.Push(usershader) + FIRST_USER_SHADER);
			}
			tex->SetShaderLayers(mlay);
		}
	}


	//==========================================================================
	//
	// Parses a shader definition
	//
	//==========================================================================

	void ParseHardwareShader()
	{
		sc.MustGetString();
		if (sc.Compare("postprocess"))
		{
			sc.MustGetString();

			PostProcessShader shaderdesc;
			TMap<FString, UserUniformValue> Uniforms;
			shaderdesc.Target = sc.String;
			shaderdesc.Target.ToLower();

			bool validTarget = false;
			if (sc.Compare("beforebloom")) validTarget = true;
			if (sc.Compare("scene")) validTarget = true;
			if (sc.Compare("screen")) validTarget = true;		
			if (!validTarget)
				sc.ScriptError("Invalid target '%s' for postprocess shader",sc.String);

			sc.MustGetToken('{');
			while (!sc.CheckToken('}'))
			{
				sc.MustGetString();
				if (sc.Compare("shader"))
				{
					sc.MustGetString();
					shaderdesc.ShaderLumpName = sc.String;

					sc.MustGetNumber();
					shaderdesc.ShaderVersion = sc.Number;
					if (sc.Number > 450 || sc.Number < 330)
						sc.ScriptError("Shader version must be in range 330 to 450!");
				}
				else if (sc.Compare("name"))
				{
					sc.MustGetString();
					shaderdesc.Name = sc.String;
				}
				else if (sc.Compare("uniform"))
				{
					bool is_cvar = false;
					bool ok = true;

					sc.MustGetString();
					FString uniformType = sc.String;
					uniformType.ToLower();

					sc.MustGetString();
					FString uniformName = sc.String;

					UniformType parsedType = UniformType::Undefined;

					if (uniformType.Compare("int") == 0)
					{
						parsedType = UniformType::Int;
					}
					else if (uniformType.Compare("float") == 0)
					{
						parsedType = UniformType::Float;
					}
					else if (uniformType.Compare("vec2") == 0)
					{
						parsedType = UniformType::Vec2;
					}
					else if (uniformType.Compare("vec3") == 0)
					{
						parsedType = UniformType::Vec3;
					}
					else if (uniformType.Compare("vec4") == 0)
					{
						parsedType = UniformType::Vec4;
					}
					else
					{
						sc.ScriptError("Unrecognized uniform type '%s'", sc.String);
						ok = false;
					}

					double Values[4] = {0.0, 0.0, 0.0, 1.0};

					if(ok && sc.CheckToken('='))
					{
						if(sc.CheckString("cvar"))
						{
							is_cvar = true;
						}
						else switch(parsedType)
						{
						case UniformType::Int:
							sc.MustGetNumber();
							Values[0] = sc.BigNumber;
							break;
						case UniformType::Float:
							sc.MustGetFloat();
							Values[0] = sc.Float;
							break;
						case UniformType::Vec2:
							sc.MustGetFloat();
							Values[0] = sc.Float;
							sc.MustGetFloat();
							Values[1] = sc.Float;
							break;
						case UniformType::Vec3:
							sc.MustGetFloat();
							Values[0] = sc.Float;
							sc.MustGetFloat();
							Values[1] = sc.Float;
							sc.MustGetFloat();
							Values[2] = sc.Float;
							break;
						case UniformType::Vec4:
							sc.MustGetFloat();
							Values[0] = sc.Float;
							sc.MustGetFloat();
							Values[1] = sc.Float;
							sc.MustGetFloat();
							Values[2] = sc.Float;
							sc.MustGetFloat();
							Values[3] = sc.Float;
							break;

						}
					}

					if (ok && !is_cvar)
					{
						Uniforms[uniformName].Type = parsedType;
						Uniforms[uniformName].Values[0] = Values[0];
						Uniforms[uniformName].Values[1] = Values[1];
						Uniforms[uniformName].Values[2] = Values[2];
						Uniforms[uniformName].Values[3] = Values[3];
					}

					if (ok && is_cvar)
					{
						if (shaderdesc.Name.IsEmpty())
							sc.ScriptError("Shader must have a name to use cvar uniforms");

						int cvarflags = CVAR_MOD|CVAR_ARCHIVE|CVAR_VIRTUAL;
						FBaseCVar *cvar;
						FString cvarname;
						void (*callback)(FBaseCVar&) = nullptr;

						if(ok)
						{
							sc.MustGetString();
							cvarname = sc.String;
							cvar = FindCVar(cvarname.GetChars(), NULL);

							if (!cvar)
							{
								sc.ScriptMessage("Unknown cvar passed to cvar_uniform");
								ok = false;
							}
						}

						if(ok)
						{
							switch (cvar->GetRealType())
							{
							case CVAR_Int:
								if(parsedType == UniformType::Int)
								{
									callback = (void (*)(FBaseCVar&))(&pp_uniform_callback1i<FIntCVar>);
									Values[0] = cvar->GetGenericRep(CVAR_Int).Int;
								}
								else if(parsedType == UniformType::Float)
								{
									callback = (void (*)(FBaseCVar&))(&pp_uniform_callback1f<FIntCVar>);
									Values[0] = cvar->GetGenericRep(CVAR_Int).Int;
								}
								else
								{
									sc.ScriptError("CVar '%s' type (int) is not convertible to uniform type (%s), must be int or float", cvarname.GetChars(), uniformType.GetChars());
									ok = false;
								}
								break;
							case CVAR_Float:
								if(parsedType == UniformType::Int)
								{
									callback = (void (*)(FBaseCVar&))(&pp_uniform_callback1i<FFloatCVar>);
									Values[0] = cvar->GetGenericRep(CVAR_Float).Float;
								}
								else if(parsedType == UniformType::Float)
								{
									callback = (void (*)(FBaseCVar&))(&pp_uniform_callback1f<FFloatCVar>);
									Values[0] = cvar->GetGenericRep(CVAR_Float).Float;
								}
								else
								{
									sc.ScriptError("CVar '%s' type (float) is not convertible to uniform type (%s), must be int or float", cvarname.GetChars(), uniformType.GetChars());
									ok = false;
								}
								break;
							case CVAR_Color:
								if(parsedType == UniformType::Vec3)
								{
									callback = (void (*)(FBaseCVar&))(&pp_uniform_callback_color3f);

									PalEntry col;
									col.d = cvar->GetGenericRep(CVAR_Int).Int;
									Values[0] = col.r / 255.0;
									Values[1] = col.g / 255.0;
									Values[2] = col.b / 255.0;
								}
								else if(parsedType == UniformType::Vec4)
								{
									callback = (void (*)(FBaseCVar&))(&pp_uniform_callback_color4f);

									PalEntry col;
									col.d = cvar->GetGenericRep(CVAR_Int).Int;
									Values[0] = col.r / 255.0;
									Values[1] = col.g / 255.0;
									Values[2] = col.b / 255.0;
									Values[3] = col.a / 255.0;
								}
								else
								{
									sc.ScriptError("CVar '%s' type (color) is not convertible to uniform type (%s), must be vec3 or vec4", cvarname.GetChars(), uniformType.GetChars());
									ok = false;
								}
								break;
							default:
								sc.ScriptError("CVar '%s' type not supported for uniforms!", cvarname.GetChars());
								ok = false;
								break;
							}
						}

						if(ok)
						{
							ExtraUniformCVARData* oldextra = (ExtraUniformCVARData*)cvar->GetExtraDataPointer2();

							ExtraUniformCVARData* extra = new ExtraUniformCVARData;
							extra->Shader = shaderdesc.Name.GetChars();
							extra->Uniform = uniformName.GetChars();
							extra->OldCallback = oldextra ? oldextra->OldCallback : cvar->m_Callback;
							extra->Next = oldextra;

							cvar->SetCallback(callback);
							cvar->SetExtraDataPointer2(extra);

							Uniforms[uniformName].Type = parsedType;
							Uniforms[uniformName].Values[0] = Values[0];
							Uniforms[uniformName].Values[1] = Values[1];
							Uniforms[uniformName].Values[2] = Values[2];
							Uniforms[uniformName].Values[3] = Values[3];
						}
					}
				}
				else if (sc.Compare("texture"))
				{
					sc.MustGetString();
					FString textureName = sc.String;

					sc.MustGetString();
					FString textureSource = sc.String;

					shaderdesc.Textures[textureName] = textureSource;
				}
				else if (sc.Compare("enabled"))
				{
					shaderdesc.Enabled = true;
				}
				else
				{
					sc.ScriptError("Unknown keyword '%s'", sc.String);
				}
			}

			auto index = PostProcessShaders.Push(shaderdesc);
			PostProcessShaders[index].Uniforms.LoadUniforms(Uniforms);
		}
		else
		{
			ETextureType type = ETextureType::Any;

			if (sc.Compare("texture")) type = ETextureType::Wall;
			else if (sc.Compare("flat")) type = ETextureType::Flat;
			else if (sc.Compare("sprite")) type = ETextureType::Sprite;
			else sc.UnGet();

			bool disable_fullbright = false;
			bool thiswad = false;
			bool iwad = false;
			bool no_mipmap = false;
			int maplump = -1;
			UserShaderDesc desc;
			desc.shaderType = SHADER_Default;
			TArray<FString> texNameList;
			TArray<int> texNameIndex;
			float speed = 1.f;

			sc.MustGetString();
			FTextureID no = TexMan.CheckForTexture(sc.String, type);
			auto tex = TexMan.GetGameTexture(no);
			if (tex) tex->AddAutoMaterials();
			MaterialLayers mlay = { -1000, -1000 };

			sc.MustGetToken('{');
			while (!sc.CheckToken('}'))
			{
				sc.MustGetString();
				if (sc.Compare("shader"))
				{
					sc.MustGetString();
					desc.shader = sc.String;
				}
				else if (sc.Compare("material"))
				{
					sc.MustGetString();
					static MaterialShaderIndex typeIndex[6] = { SHADER_Default, SHADER_Default, SHADER_Specular, SHADER_Specular, SHADER_PBR, SHADER_PBR };
					static bool usesBrightmap[6] = { false, true, false, true, false, true };
					static const char *typeName[6] = { "normal", "brightmap", "specular", "specularbrightmap", "pbr", "pbrbrightmap" };
					bool found = false;
					for (int i = 0; i < 6; i++)
					{
						if (sc.Compare(typeName[i]))
						{
							desc.shaderType = typeIndex[i];
							found = true;
							break;
						}
					}
					if (!found)
						sc.ScriptError("Unknown material type '%s' specified\n", sc.String);
				}
				else if (sc.Compare("nomipmap"))
				{
					no_mipmap = true;
				}
				else if (sc.Compare("speed"))
				{
					sc.MustGetFloat();
					speed = float(sc.Float);
				}
				else if (sc.Compare("texture"))
				{
					sc.MustGetString();
					FString textureName = sc.String;
					for(FString &texName : texNameList)
					{
						if(!texName.Compare(textureName))
						{
							sc.ScriptError("Trying to redefine custom hardware shader texture '%s' in texture '%s'\n", textureName.GetChars(), tex? tex->GetName().GetChars() : "(null)");
						}
					}
					sc.MustGetString();
					bool okay = false;
					size_t texIndex = 0;
					for (size_t i = 0; i < countof(mlay.CustomShaderTextures); i++)
					{
						if (!mlay.CustomShaderTextures[i])
						{
							mlay.CustomShaderTextureSampling[texIndex] = MaterialLayerSampling::Default;
							mlay.CustomShaderTextures[i] = TexMan.FindGameTexture(sc.String, ETextureType::Any, FTextureManager::TEXMAN_TryAny);
							if (!mlay.CustomShaderTextures[i])
							{
								sc.ScriptError("Custom hardware shader texture '%s' not found in texture '%s'\n", sc.String, tex? tex->GetName().GetChars() : "(null)");
							}

							texNameList.Push(textureName);
							texNameIndex.Push((int)i);
							texIndex = i;
							okay = true;
							break;
						}
					}
					if(!okay)
					{
						sc.ScriptError("Error: out of texture units in texture '%s'", tex? tex->GetName().GetChars() : "(null)");
					}
				}
				else if(sc.Compare("define"))
				{
					sc.MustGetString();
					FString defineName = sc.String;
					FString defineValue = "";
					if(sc.CheckToken('='))
					{
						sc.MustGetString();
						defineValue = sc.String;
					}
					desc.defines.AppendFormat("#define %s %s\n", defineName.GetChars(), defineValue.GetChars());
				}
				else if (sc.Compare("disablealphatest"))
				{
					desc.disablealphatest = true;
				}
			}
			if (!tex)
			{
				return;
			}

			tex->SetNoMipmap(no_mipmap);

			int firstUserTexture;
			switch (desc.shaderType)
			{
			default:
			case SHADER_Default: firstUserTexture = 5; break;
			case SHADER_Specular: firstUserTexture = 7; break;
			case SHADER_PBR: firstUserTexture = 9; break;
			}

			for (unsigned int i = 0; i < texNameList.Size(); i++)
			{
				desc.defines.AppendFormat("#define %s texture%d\n", texNameList[i].GetChars(), texNameIndex[i] + firstUserTexture);
			}

			if (desc.shader.IsNotEmpty())
			{
				if (tex->isWarped() != 0)
				{
					Printf("Cannot combine warping with hardware shader on texture '%s'\n", tex->GetName().GetChars());
					return;
				}
				tex->SetShaderSpeed(speed);
				for (unsigned i = 0; i < usershaders.Size(); i++)
				{
					if (!usershaders[i].shader.CompareNoCase(desc.shader) &&
						usershaders[i].shaderType == desc.shaderType &&
						usershaders[i].shaderFlags == desc.shaderFlags &&
						!usershaders[i].defines.Compare(desc.defines))
					{
						SetShaderIndex(tex, i + FIRST_USER_SHADER);
						tex->SetShaderLayers(mlay);		
						return;
					}
				}
				SetShaderIndex(tex, usershaders.Push(desc) + FIRST_USER_SHADER);
			}
			tex->SetShaderLayers(mlay);
		}
	}

	void ParseColorization(FScanner& sc)
	{
		TextureManipulation tm = {};
		tm.ModulateColor = 0x01ffffff;
		sc.MustGetString();
		FName cname = sc.String;
		sc.MustGetToken('{');
		while (!sc.CheckToken('}'))
		{
			sc.MustGetString();
			if (sc.Compare("DesaturationFactor"))
			{
				sc.MustGetFloat();
				tm.DesaturationFactor = (float)sc.Float;
			}
			else if (sc.Compare("AddColor"))
			{
				sc.MustGetString();
				tm.AddColor = (tm.AddColor & 0xff000000) | (V_GetColor(sc) & 0xffffff);
			}
			else if (sc.Compare("ModulateColor"))
			{
				sc.MustGetString();
				tm.ModulateColor = V_GetColor(sc) & 0xffffff;
				if (sc.CheckToken(','))
				{
					sc.MustGetNumber();
					tm.ModulateColor.a = sc.Number;
				}
				else tm.ModulateColor.a = 1;
			}
			else if (sc.Compare("BlendColor"))
			{
				sc.MustGetString();
				tm.BlendColor = V_GetColor(sc) & 0xffffff;
				sc.MustGetToken(',');
				sc.MustGetString();
				static const char* opts[] = { "none", "alpha", "screen", "overlay", "hardlight", nullptr };
				tm.AddColor.a = (tm.AddColor.a & ~TextureManipulation::BlendMask) | sc.MustMatchString(opts);
				if (sc.Compare("alpha"))
				{
					sc.MustGetToken(',');
					sc.MustGetFloat();
					tm.BlendColor.a = (uint8_t)(clamp(sc.Float, 0., 1.) * 255);
				}
			}
			else if (sc.Compare("invert"))
			{
				tm.AddColor.a |= TextureManipulation::InvertBit;
			}
			else sc.ScriptError("Unknown token '%s'", sc.String);
		}
		if (tm.CheckIfEnabled())
		{
			TexMan.InsertTextureManipulation(cname, tm);
		}
		else
		{
			TexMan.RemoveTextureManipulation(cname);
		}
	}
	

public:
	//==========================================================================
	//
	//
	//
	//==========================================================================
	void DoParseDefs()
	{
		int recursion=0;
		int lump, type;

		// Get actor class name.
		while (true)
		{
			sc.SavePos();
			if (!sc.GetToken ())
			{
				return;
			}
			type = sc.MatchString(CoreKeywords);
			switch (type)
			{
			case TAG_INCLUDE:
				{
					sc.MustGetString();
					// This is not using sc.Open because it can print a more useful error message when done here
					lump = fileSystem.CheckNumForFullName(sc.String, true);
					if (lump==-1)
						sc.ScriptError("Lump '%s' not found", sc.String);

					GLDefsParser newscanner(lump, LightAssociations);
					newscanner.lightSizeFactor = lightSizeFactor;
					newscanner.DoParseDefs();
					break;
				}
			case LIGHT_POINT:
				ParsePointLight();
				break;
			case LIGHT_PULSE:
				ParsePulseLight();
				break;
			case LIGHT_FLICKER:
				ParseFlickerLight();
				break;
			case LIGHT_FLICKER2:
				ParseFlickerLight2();
				break;
			case LIGHT_SECTOR:
				ParseSectorLight();
				break;
			case LIGHT_OBJECT:
				ParseObject();
				break;
			case LIGHT_CLEAR:
				// This has been intentionally removed
				break;
			case TAG_SHADER:
				ParseShader();
				break;
			case TAG_CLEARSHADERS:
				break;
			case TAG_SKYBOX:
				ParseGldefSkybox();
				break;
			case TAG_GLOW:
				ParseGlow();
				break;
			case TAG_BRIGHTMAP:
				ParseBrightmap();
				break;
			case TAG_MATERIAL:
				ParseMaterial();
				break;
			case TAG_HARDWARESHADER:
				ParseHardwareShader();
				break;
			case TAG_DETAIL:
				ParseDetailTexture();
				break;
			case TAG_LIGHTSIZEFACTOR:
				lightSizeFactor = ParseFloat(sc);
				break;
			case TAG_DISABLE_FB:
				{
					/* not implemented.
					sc.MustGetString();
					const PClass *cls = PClass::FindClass(sc.String);
					if (cls) GetDefaultByType(cls)->renderflags |= RF_NEVERFULLBRIGHT;
					*/
				}
				break;
			case TAG_COLORIZATION:
				ParseColorization(sc);
				break;
			default:
				sc.ScriptError("Error parsing defs.  Unknown tag: %s.\n", sc.String);
				break;
			}
		}
	}
	
	GLDefsParser(int lumpnum, TArray<FLightAssociation> &la)
	 : sc(lumpnum), workingLump(lumpnum), LightAssociations(la)
	{
	}
};

//==========================================================================
//
//
//
//==========================================================================

void LoadGLDefs(const char *defsLump)
{
	TArray<FLightAssociation> LightAssociations;
	int workingLump, lastLump;
	static const char *gldefsnames[] = { "GLDEFS", defsLump, nullptr };

	lastLump = 0;
	while ((workingLump = fileSystem.FindLumpMulti(gldefsnames, &lastLump)) != -1)
	{
		GLDefsParser sc(workingLump, LightAssociations);
		sc.DoParseDefs();
	}
	InitializeActorLights(LightAssociations);
}


//==========================================================================
//
//
//
//==========================================================================

void ParseGLDefs()
{
	const char *defsLump = NULL;

	LightDefaults.DeleteAndClear();
	AttenuationIsSet = -1;
	//gl_DestroyUserShaders(); function says 'todo'
	switch (gameinfo.gametype)
	{
	case GAME_Heretic:
		defsLump = "HTICDEFS";
		break;
	case GAME_Hexen:
		defsLump = "HEXNDEFS";
		break;
	case GAME_Strife:
		defsLump = "STRFDEFS";
		break;
	case GAME_Doom:
		defsLump = "DOOMDEFS";
		break;
	case GAME_Chex:
		defsLump = "CHEXDEFS";
		break;
	default: // silence GCC
		break;
	}
	ParseVavoomSkybox();
	LoadGLDefs(defsLump);
}
