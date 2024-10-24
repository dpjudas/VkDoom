#pragma once

#include "c_cvars.h"
#include "v_palette.h"

#include "r_utility.h"

struct Colormap;

inline int hw_ClampLight(int lightlevel)
{
	return clamp(lightlevel, 0, 255);
}

EXTERN_CVAR(Int, gl_weaponlight);

inline	int getExtraLight()
{
	return r_viewpoint.extralight * gl_weaponlight;
}

inline int isFullbrightScene()
{
	player_t* cplayer = r_viewpoint.camera ? r_viewpoint.camera->player : nullptr;
	if (cplayer)
	{
		if (cplayer->extralight == INT_MIN)
		{
			return true;
		}
		else if (cplayer->fixedcolormap != NOFIXEDCOLORMAP)
		{
			return true;
		}
		else if (cplayer->fixedlightlevel != -1)
		{
			auto torchtype = PClass::FindActor(NAME_PowerTorch);
			auto litetype = PClass::FindActor(NAME_PowerLightAmp);
			for (AActor* in = cplayer->mo->Inventory; in; in = in->Inventory)
			{
				// Need special handling for light amplifiers 
				if (in->IsKindOf(torchtype))
				{
					return true;
				}
				else if (in->IsKindOf(litetype))
				{
					return true;
				}
			}
		}
		return false;
	}
	else
	{
		return false;
	}
}
