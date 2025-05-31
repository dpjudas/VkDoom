#pragma once

struct HWMeshHelper
{
	TArray<HWWall> opaque;
	TArray<HWWall> masked;
	TArray<HWWall> maskedOffset;
	TArray<HWWall> translucent;
	TArray<HWWall> translucentBorder;
	TArray<HWWall> portals;
	TArray<HWMissing> lower;
	TArray<HWMissing> upper;
	TArray<HWDecalCreateInfo> decals;
};


struct HWWallDispatcher
{
	FLevelLocals* Level;
	HWDrawInfo* di;
	HWMeshHelper* mh;
	ELightMode lightmode;

	HWWallDispatcher(HWDrawInfo* info)
	{
		Level = info->Level;
		di = info;
		mh = nullptr;
		lightmode = info->lightmode;
	}

	HWWallDispatcher(FLevelLocals* lev, HWMeshHelper* help, ELightMode lm)
	{
		Level = lev;
		di = nullptr;
		mh = help;
		lightmode = lm;
	}

	void AddUpperMissingTexture(side_t* side, subsector_t* sub, float height)
	{
		if (di) di->AddUpperMissingTexture(side, sub, height);
		else
		{
			mh->upper.Reserve(1);
			mh->upper.Last() = { side, sub, height };
		}
	}
	void AddLowerMissingTexture(side_t* side, subsector_t* sub, float height)
	{
		if (di) di->AddLowerMissingTexture(side, sub, height);
		else
		{
			mh->lower.Reserve(1);
			mh->lower.Last() = { side, sub, height };
		}
	}

	bool isFullbrightScene()
	{
		// The mesh builder cannot know this and must treat everything as not fullbright.
		return di && di->isFullbrightScene();
	}

	void AddWall(HWWall* wall)
	{
		if (di) di->AddWall(wall);
		else
		{
			if (wall->flags & HWWall::HWF_TRANSLUCENT)
			{
				mh->translucent.Push(*wall);
			}
			else
			{
				if (wall->flags & HWWall::HWF_SKYHACK && wall->type == RENDERWALL_M2S)
				{
					mh->maskedOffset.Push(*wall);
				}
				else
				{
					bool masked = HWWall::passflag[wall->type] == 1 ? false : (wall->texture && wall->texture->isMasked());
					if (masked)
						mh->masked.Push(*wall);
					else
						mh->opaque.Push(*wall);
				}
			}
		}
	}

	void AddPortal(HWWall* wal)
	{
		mh->portals.Push(*wal);
	}

	void AddDecal(const HWDecalCreateInfo& info)
	{
		mh->decals.Push(info);
	}
};
