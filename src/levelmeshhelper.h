#pragma once
// level mesh helper
// this is a minimal include to keep the other source files a little bit leaner

struct UpdateLevelMesh
{
	virtual void FloorHeightChanged(struct sector_t *sector) = 0;
	virtual void CeilingHeightChanged(struct sector_t *sector) = 0;
	virtual void MidTex3DHeightChanged(struct sector_t *sector) = 0;

	virtual void FloorTextureChanged(struct sector_t *sector) = 0;
	virtual void CeilingTextureChanged(struct sector_t *sector) = 0;

	virtual void SectorChangedOther(struct sector_t *sector) = 0;

	virtual void SideTextureChanged(struct side_t *side, int section) = 0;

	virtual void SectorLightChanged(struct sector_t* sector) = 0;
	virtual void SectorLightThinkerCreated(struct sector_t* sector, class DLighting* lightthinker) = 0;
	virtual void SectorLightThinkerDestroyed(struct sector_t* sector, class DLighting* lightthinker) = 0;
};

extern UpdateLevelMesh* LevelMeshUpdater;

void SetNullLevelMeshUpdater();
