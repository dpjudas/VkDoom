#pragma once

struct HWFlatMeshHelper
{
	TArray<HWFlat> list;
	TArray<HWFlat> translucent;
	TArray<HWFlat> translucentborder;
	TArray<HWFlat> portals;
	uint8_t section_renderflags = 0;
};

struct HWFlatDispatcher
{
	FLevelLocals* Level;
	HWDrawInfo* di;
	HWFlatMeshHelper* mh;
	ELightMode lightmode;

	HWFlatDispatcher(HWDrawInfo* info)
	{
		Level = info->Level;
		di = info;
		mh = nullptr;
		lightmode = info->lightmode;
	}

	HWFlatDispatcher(FLevelLocals* lev, HWFlatMeshHelper* help, ELightMode lm)
	{
		Level = lev;
		di = nullptr;
		mh = help;
		lightmode = lm;
	}

	bool isFullbrightScene()
	{
		// The mesh builder cannot know this and must treat everything as not fullbright.
		return di && di->isFullbrightScene();
	}

	void AddFlat(HWFlat* flat, bool fog)
	{
		if (di)
		{
			di->AddFlat(flat, fog);
		}
		else if (flat->plane.texture == skyflatnum || flat->stack)
		{
			mh->portals.Push(*flat);
		}
		else
		{
			if (flat->renderstyle != STYLE_Translucent || flat->alpha < 1.f - FLT_EPSILON || fog || flat->texture == nullptr)
			{
				// translucent 3D floors go into the regular translucent list, translucent portals go into the translucent border list.
				if (flat->renderflags & SSRF_RENDER3DPLANES)
					mh->translucent.Push(*flat);
				else
					mh->translucentborder.Push(*flat);
			}
			else if (flat->texture->GetTranslucency())
			{
				if (flat->stack)
				{
					mh->translucentborder.Push(*flat);
				}
				else if ((flat->renderflags & SSRF_RENDER3DPLANES) && !flat->plane.plane.isSlope())
				{
					mh->translucent.Push(*flat);
				}
				else
				{
					mh->list.Push(*flat);
				}
			}
			else
			{
				bool masked = flat->texture->isMasked() && ((flat->renderflags & SSRF_RENDER3DPLANES) || flat->stack);
				if (masked)
				{
					mh->list.Push(*flat); // Do we need a different list for this?
				}
				else
				{
					mh->list.Push(*flat);
				}
			}
		}
	}
};
