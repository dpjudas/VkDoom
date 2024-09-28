#pragma once
// level mesh helper
// this is a minimal include to keep the other source files a little bit leaner

struct sector_t;
struct side_t;
class DLighting;

struct UpdateLevelMesh
{
	virtual void FloorHeightChanged(sector_t *sector) = 0;
	virtual void CeilingHeightChanged(sector_t *sector) = 0;
	virtual void MidTex3DHeightChanged(sector_t *sector) = 0;

	virtual void FloorTextureChanged(sector_t *sector) = 0;
	virtual void CeilingTextureChanged(sector_t *sector) = 0;

	virtual void SectorChangedOther(sector_t *sector) = 0;

	virtual void SideTextureChanged(side_t *side, int section) = 0;
	virtual void SideDecalsChanged(side_t* side) = 0;

	virtual void SectorLightChanged(sector_t* sector) = 0;
	virtual void SectorLightThinkerCreated(sector_t* sector, DLighting* lightthinker) = 0;
	virtual void SectorLightThinkerDestroyed(sector_t* sector, DLighting* lightthinker) = 0;
};

extern UpdateLevelMesh* LevelMeshUpdater;

void SetNullLevelMeshUpdater();
