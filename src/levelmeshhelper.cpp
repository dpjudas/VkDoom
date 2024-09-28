
#include "levelmeshhelper.h"

struct NullLevelMeshUpdater : UpdateLevelMesh
{
	void FloorHeightChanged(sector_t* sector) override {}
	void CeilingHeightChanged(sector_t* sector) override {}
	void MidTex3DHeightChanged(sector_t* sector) override {}

	void FloorTextureChanged(sector_t* sector) override {}
	void CeilingTextureChanged(sector_t* sector) override {}

	void SectorChangedOther(sector_t* sector) override {}

	void SideTextureChanged(side_t* side, int section) override {}
	void SideDecalsChanged(side_t* side) override {}

	void SectorLightChanged(sector_t* sector) override {}
	void SectorLightThinkerCreated(sector_t* sector, DLighting* lightthinker) override {}
	void SectorLightThinkerDestroyed(sector_t* sector, DLighting* lightthinker) override {}
};

static NullLevelMeshUpdater nullUpdater;

UpdateLevelMesh* LevelMeshUpdater = &nullUpdater;

void SetNullLevelMeshUpdater()
{
	LevelMeshUpdater = &nullUpdater;
}
