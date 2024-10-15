#pragma once
// level mesh helper
// this is a minimal include to keep the other source files a little bit leaner

struct sector_t;
struct side_t;
class DLighting;

struct UpdateLevelMesh
{
	//3d floor-aware
	void FloorHeightChanged(sector_t *sector);
	void CeilingHeightChanged(sector_t *sector);
	void MidTex3DHeightChanged(sector_t *sector);

	void FloorTextureChanged(sector_t *sector);
	void CeilingTextureChanged(sector_t *sector);

	void SectorChangedOther(sector_t *sector);

	void SideTextureChanged(side_t *side, int section);
	void SideDecalsChanged(side_t* side);

	void SectorLightChanged(sector_t* sector);
	void SectorLightThinkerCreated(sector_t* sector, DLighting* lightthinker);
	void SectorLightThinkerDestroyed(sector_t* sector, DLighting* lightthinker);
	
	//raw
	virtual void OnFloorHeightChanged(sector_t *sector) = 0;
	virtual void OnCeilingHeightChanged(sector_t *sector) = 0;
	virtual void OnMidTex3DHeightChanged(sector_t *sector) = 0;

	virtual void OnFloorTextureChanged(sector_t *sector) = 0;
	virtual void OnCeilingTextureChanged(sector_t *sector) = 0;

	virtual void OnSectorChangedOther(sector_t *sector) = 0;

	virtual void OnSideTextureChanged(side_t *side, int section) = 0;
	virtual void OnSideDecalsChanged(side_t* side) = 0;

	virtual void OnSectorLightChanged(sector_t* sector) = 0;
	virtual void OnSectorLightThinkerCreated(sector_t* sector, DLighting* lightthinker) = 0;
	virtual void OnSectorLightThinkerDestroyed(sector_t* sector, DLighting* lightthinker) = 0;
};

extern UpdateLevelMesh* LevelMeshUpdater;

void SetNullLevelMeshUpdater();
