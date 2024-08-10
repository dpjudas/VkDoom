#pragma once
// level mesh helper
// this is a minimal include to keep the other source files a little bit leaner

struct UpdateLevelMesh
{
	virtual void SectorChanged(void *sector) = 0;

	virtual void SideChanged(void *side) = 0;
};

extern UpdateLevelMesh* LevelMeshUpdater;
