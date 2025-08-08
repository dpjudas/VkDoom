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
#include "v_video.h"
#include "v_draw.h"
#include "s_music.h"

// Hexen startup screen
#define ST_PROGRESS_X			64			// Start of notches x screen pos.
#define ST_PROGRESS_Y			441			// Start of notches y screen pos.


class FGenericStartScreen : public FStartScreen
{
	FBitmap Background;
	int NotchPos = 0;

public:
	FGenericStartScreen(int max_progress);

	bool DoProgress(int) override;
};


//==========================================================================
//
// FGenericStartScreen Constructor
//
// Shows the Hexen startup screen. If the screen doesn't appear to be
// valid, it sets hr for a failure.
//
// The startup graphic is a planar, 4-bit 640x480 graphic preceded by a
// 16 entry (48 byte) VGA palette.
//
//==========================================================================

FGenericStartScreen::FGenericStartScreen(int max_progress)
	: FStartScreen(max_progress)
{
	StartupBitmap.Create(640 * 2, 480 * 2);
	ClearBlock(StartupBitmap, { 0, 0, 0, 255 }, 0, 0, 640 * 2, 480 * 2);

	AddImage("BOOTLOGO", [](FGameTexture* tex) {
		double scrwidth = screen->GetWidth();
		double scrheight = screen->GetHeight();
		double scale = scrheight / 2160.0;
		double imgwidth = tex->GetDisplayWidth() * scale;
		double imgheight = tex->GetDisplayHeight() * scale;
		double imgx = (scrwidth - imgwidth) * 0.5;
		double imgy = (scrheight - imgheight) * 0.5;

		DrawTexture(twod, tex, imgx, imgy, DTA_DestWidthF, imgwidth, DTA_DestHeightF, imgheight, DTA_VirtualWidthF, scrwidth, DTA_VirtualHeightF, scrheight, DTA_KeepRatio, true, TAG_END);
		});

	if (!batchrun)
	{
		if (GameStartupInfo.Song.IsNotEmpty())
		{
			S_ChangeMusic(GameStartupInfo.Song.GetChars(), true, true);
		}
	}
}

//==========================================================================
//
// FGenericStartScreen :: Progress
//
// Bumps the progress meter one notch.
//
//==========================================================================

bool FGenericStartScreen::DoProgress(int advance)
{
	int notch_pos;

	if (CurPos < MaxPos)
	{
		RgbQuad bcolor = { 2, 25, 87, 255 }; // todo: make configurable
		int numnotches = 200 * 2;
		notch_pos = ((CurPos + 1) * numnotches) / MaxPos;
		if (notch_pos != NotchPos)
		{ // Time to draw another notch.
			ClearBlock(StartupBitmap, bcolor, (320 - 100) * 2, 480 * 2 - 30, notch_pos, 4 * 2);
			NotchPos = notch_pos;
			if (StartupTexture)
				StartupTexture->CleanHardwareData(true);
		}
	}
	return FStartScreen::DoProgress(advance);
}


FStartScreen* CreateGenericStartScreen(int max_progress)
{
	return new FGenericStartScreen(max_progress);
}
