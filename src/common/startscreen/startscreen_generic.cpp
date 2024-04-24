/*
** st_start.cpp
** Generic startup screen
**
**---------------------------------------------------------------------------
** Copyright 2022 Christoph Oelckers
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

#include "startscreen.h"
#include "filesystem.h"
#include "printf.h"
#include "startupinfo.h"
#include "image.h"
#include "texturemanager.h"
#include "i_time.h"
#include "v_video.h"
#include "v_draw.h"
#include "vm.h"

void DoDrawTexture(F2DDrawer* drawer, FGameTexture* img, double x, double y, VMVa_List& args);

static const char* DTA_Constants[] =
{
	"DTA_Base",
	"DTA_DestWidth",
	"DTA_DestHeight",
	"DTA_Alpha",
	"DTA_FillColor",
	"DTA_TranslationIndex",
	"DTA_AlphaChannel",
	"DTA_Clean",
	"DTA_320x200",
	"DTA_Bottom320x200",
	"DTA_CleanNoMove",
	"DTA_CleanNoMove_1",
	"DTA_FlipX",
	"DTA_ShadowColor",
	"DTA_ShadowAlpha",
	"DTA_Shadow",
	"DTA_VirtualWidth",
	"DTA_VirtualHeight",
	"DTA_TopOffset",
	"DTA_LeftOffset",
	"DTA_CenterOffset",
	"DTA_CenterBottomOffset",
	"DTA_WindowLeft",
	"DTA_WindowRight",
	"DTA_ClipTop",
	"DTA_ClipBottom",
	"DTA_ClipLeft",
	"DTA_ClipRight",
	"DTA_Masked",
	"DTA_HUDRules",
	"DTA_HUDRulesC",
	"DTA_KeepRatio",
	"DTA_RenderStyle",
	"DTA_ColorOverlay",
	"DTA_BilinearFilter",
	"DTA_SpecialColormap",
	"DTA_Desaturate",
	"DTA_Fullscreen",
	"DTA_DestWidthF",
	"DTA_DestHeightF",
	"DTA_TopOffsetF",
	"DTA_LeftOffsetF",
	"DTA_VirtualWidthF",
	"DTA_VirtualHeightF",
	"DTA_WindowLeftF",
	"DTA_WindowRightF",
	"DTA_TextLen",
	"DTA_CellX",
	"DTA_CellY",
	"DTA_Color",
	"DTA_FlipY",
	"DTA_SrcX",
	"DTA_SrcY",
	"DTA_SrcWidth",
	"DTA_SrcHeight",
	"DTA_LegacyRenderStyle",
	"DTA_Burn",
	"DTA_Spacing",
	"DTA_Monospace",
	"DTA_FullscreenEx",
	"DTA_FullscreenScale",
	"DTA_ScaleX",
	"DTA_ScaleY",
	"DTA_ViewportX",
	"DTA_ViewportY",
	"DTA_ViewportWidth",
	"DTA_ViewportHeight",
	"DTA_CenterOffsetRel",
	"DTA_TopLeft",
	"DTA_Pin",
	"DTA_Rotate",
	"DTA_FlipOffsets",
	"DTA_Indexed",
	"DTA_CleanTop",
	"DTA_NoOffset",
	"DTA_Localize",
};

struct DrawTextureEntry
{
	FGameTexture* tex = nullptr;
	FString texname;
	double x = 0.0;
	double y = 0.0;
	TArray<VMValue> args;
	TArray<uint8_t> reginfo;
};

class FGenericStartScreen : public FStartScreen
{
public:
	FGenericStartScreen(int max_progress);
	void RenderScreen(F2DDrawer* drawer) override;

private:
	void ValidateTexture();

	bool Validated = false;
	TArray<DrawTextureEntry> DrawCommands;
	TMap<FString, FGameTexture*> Textures;
};

FStartScreen* CreateGenericStartScreen(int max_progress)
{
	return new FGenericStartScreen(max_progress);
}

FGenericStartScreen::FGenericStartScreen(int max_progress)
{
	FScanner sc;
	int numblocks = 0;

	int i = 0;
	TMap<FString, int> DTALookup;
	for (const char* constant : DTA_Constants)
	{
		DTALookup.Insert(FString(constant).MakeLower(), DTA_Base + i);
		i++;
	}

	int lump = fileSystem.CheckNumForFullName("STARTUPINFO", true, 0, true);
	if (lump == -1)
		return;

	sc.OpenLumpNum(lump);
	while (sc.GetString())
	{
		if (sc.Compare("background"))
		{
			numblocks++;
			if (numblocks > 1)
			{
				sc.ScriptMessage("Multiple 'background' records ignored");
				// Skip the rest.
				break;
			}

			sc.MustGetStringName("{");
			while (!sc.CheckString("}"))
			{
				sc.MustGetString();
				if (sc.Compare("DrawTexture"))
				{
					DrawTextureEntry entry;

					sc.MustGetString();

					entry.texname = sc.String;

					sc.MustGetFloat();
					entry.x = sc.Float;

					sc.MustGetFloat();
					entry.y = sc.Float;

					while (!sc.CheckString("TAG_END"))
					{
						if (sc.CheckNumber())
						{
							entry.args.Push(VMValue(sc.Number));
							entry.reginfo.Push(REGT_INT);
						}
						else if (sc.CheckFloat())
						{
							entry.args.Push(VMValue(sc.Float));
							entry.reginfo.Push(REGT_FLOAT);
						}
						else
						{
							sc.MustGetString();
							int* value = DTALookup.CheckKey(FString(sc.String).MakeLower());
							if (value)
							{
								entry.args.Push(VMValue(*value));
								entry.reginfo.Push(REGT_INT);
							}
							else
							{
								sc.ScriptError("Unknown DTA constant '%s'", sc.String);
								break;
							}
						}
					}
					DrawCommands.Push(entry);
				}
			}
		}
	}
}

void FGenericStartScreen::RenderScreen(F2DDrawer* drawer)
{
	ValidateTexture();

	for (const DrawTextureEntry& command : DrawCommands)
	{
		if (command.tex)
		{
			VMVa_List args = {};
			args.args = command.args.Data();
			args.reginfo = command.reginfo.Data();
			args.numargs = command.args.Size();
			args.curindex = 0;
			DoDrawTexture(drawer, command.tex, command.x, command.y, args);
		}
	}
}

void FGenericStartScreen::ValidateTexture()
{
	if (Validated)
		return;

	Validated = true;
	for (DrawTextureEntry& command : DrawCommands)
	{
		FString key = command.texname.MakeLower();
		if (!Textures.CheckKey(key))
		{
			int lump = fileSystem.CheckNumForName(key.GetChars(), FileSys::ns_graphics);
			if (lump != -1)
			{
				auto imgsource = FImageSource::GetImage(lump, false);
				Textures[key] = MakeGameTexture(new FImageTexture(imgsource), nullptr, ETextureType::Override);
			}
			else
			{
				Textures[key] = nullptr;
			}
		}
		command.tex = Textures[key];
	}
}
