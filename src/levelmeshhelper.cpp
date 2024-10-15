
#include "levelmeshhelper.h"
#include "r_defs.h"
#include "g_levellocals.h"

#define PROPAGATE_CORRELATIONS_SECTOR(func)\
	func(sector);\
	TArray<int>* c = sector->Level->SecCorrelations.CheckKey(sector->Index());\
	if(c) for(int index : *c) func(&sector->Level->sectors[index]);

#define PROPAGATE_CORRELATIONS_SECTOR2(func, arg2)\
	func(sector, arg2);\
	TArray<int>* c = sector->Level->SecCorrelations.CheckKey(sector->Index());\
	if(c) for(int index : *c) func(&sector->Level->sectors[index], arg2);

void UpdateLevelMesh::FloorHeightChanged(sector_t* sector)
{
	PROPAGATE_CORRELATIONS_SECTOR(OnFloorHeightChanged);
}

void UpdateLevelMesh::CeilingHeightChanged(sector_t* sector)
{
	PROPAGATE_CORRELATIONS_SECTOR(OnCeilingHeightChanged);
}

void UpdateLevelMesh::MidTex3DHeightChanged(sector_t* sector)
{
	PROPAGATE_CORRELATIONS_SECTOR(OnMidTex3DHeightChanged);
}

void UpdateLevelMesh::FloorTextureChanged(sector_t* sector)
{
	PROPAGATE_CORRELATIONS_SECTOR(OnFloorTextureChanged);
}
void UpdateLevelMesh::CeilingTextureChanged(sector_t* sector)
{
	PROPAGATE_CORRELATIONS_SECTOR(OnCeilingTextureChanged);
}

void UpdateLevelMesh::SectorChangedOther(sector_t* sector)
{
	PROPAGATE_CORRELATIONS_SECTOR(OnSectorChangedOther);
}

void UpdateLevelMesh::SideTextureChanged(side_t* side, int section)
{
	OnSideTextureChanged(side, section);

	TArray<int>* c = side->sector->Level->SecCorrelations.CheckKey(side->sector->Index());
	// if midtex changed, and is a 3d floor that should use the midtexture (and not the line itself's upper/lower), propagate change to all lines in affected sectors
	if(c && section == 1 && side->sector->Sec3dControlUseMidTex)
	{
		for(int index : *c)
		{
			for(auto line : side->sector->Level->sectors[index].Lines)
			{
				OnSideTextureChanged(line->sidedef[0], section); // should only need the first side?
				// OnSideTextureChanged(&line->sidedef[1], section);
			}
		}
	}
}

void UpdateLevelMesh::SideDecalsChanged(side_t* side)
{ // Decals shouldn't propagate
	return OnSideDecalsChanged(side);
}

void UpdateLevelMesh::SectorLightChanged(sector_t* sector)
{
	PROPAGATE_CORRELATIONS_SECTOR(OnSectorLightChanged);
}

void UpdateLevelMesh::SectorLightThinkerCreated(sector_t* sector, DLighting* lightthinker)
{
	PROPAGATE_CORRELATIONS_SECTOR2(OnSectorLightThinkerCreated, lightthinker);
}

void UpdateLevelMesh::SectorLightThinkerDestroyed(sector_t* sector, DLighting* lightthinker)
{
	PROPAGATE_CORRELATIONS_SECTOR2(OnSectorLightThinkerDestroyed, lightthinker);
}

struct NullLevelMeshUpdater : UpdateLevelMesh
{
	void OnFloorHeightChanged(sector_t* sector) override {}
	void OnCeilingHeightChanged(sector_t* sector) override {}
	void OnMidTex3DHeightChanged(sector_t* sector) override {}

	void OnFloorTextureChanged(sector_t* sector) override {}
	void OnCeilingTextureChanged(sector_t* sector) override {}

	void OnSectorChangedOther(sector_t* sector) override {}

	void OnSideTextureChanged(side_t* side, int section) override {}
	void OnSideDecalsChanged(side_t* side) override {}

	void OnSectorLightChanged(sector_t* sector) override {}
	void OnSectorLightThinkerCreated(sector_t* sector, DLighting* lightthinker) override {}
	void OnSectorLightThinkerDestroyed(sector_t* sector, DLighting* lightthinker) override {}
};

static NullLevelMeshUpdater nullUpdater;

UpdateLevelMesh* LevelMeshUpdater = &nullUpdater;

void SetNullLevelMeshUpdater()
{
	LevelMeshUpdater = &nullUpdater;
}
