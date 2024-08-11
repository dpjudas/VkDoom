
#include "levelmeshhelper.h"

struct NullLevelMeshUpdater : UpdateLevelMesh
{
	virtual void FloorHeightChanged(struct sector_t* sector) {};
	virtual void CeilingHeightChanged(struct sector_t* sector) {};
	virtual void MidTex3DHeightChanged(struct sector_t* sector) {};

	virtual void FloorTextureChanged(struct sector_t* sector) {};
	virtual void CeilingTextureChanged(struct sector_t* sector) {};

	virtual void SectorChangedOther(struct sector_t* sector) {};

	virtual void SideTextureChanged(struct side_t* side, int section) {};

	virtual void SectorLightChanged(struct sector_t* sector) {};
	virtual void SectorLightThinkerCreated(struct sector_t* sector, class DLighting* lightthinker) {};
	virtual void SectorLightThinkerDestroyed(struct sector_t* sector, class DLighting* lightthinker) {};
};

static NullLevelMeshUpdater nullUpdater;

UpdateLevelMesh* LevelMeshUpdater = &nullUpdater;

void SetNullLevelMeshUpdater()
{
	LevelMeshUpdater = &nullUpdater;
}
