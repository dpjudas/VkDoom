
#include "levelmeshhelper.h"
#include "r_defs.h"
#include "g_levellocals.h"

template<typename Self, typename Fn, typename... Args>
void PropagateCorrelations(Self self, Fn fn, sector_t *sector, Args... args)
{
	(self->*fn)(sector, args...);
	
	TArray<int>* c = sector->Level->SecCorrelations.CheckKey(sector->Index());

	if(c)
	{
		for(int index : *c)
		{
			(self->*fn)(&sector->Level->sectors[index], args...);
		}
	}

}

void UpdateLevelMesh::FloorHeightChanged(sector_t* sector)
{
	PropagateCorrelations(this, &UpdateLevelMesh::OnFloorHeightChanged, sector);
}

void UpdateLevelMesh::CeilingHeightChanged(sector_t* sector)
{
	PropagateCorrelations(this, &UpdateLevelMesh::OnCeilingHeightChanged, sector);
}

void UpdateLevelMesh::MidTex3DHeightChanged(sector_t* sector)
{
	PropagateCorrelations(this, &UpdateLevelMesh::OnMidTex3DHeightChanged, sector);
}

void UpdateLevelMesh::FloorTextureChanged(sector_t* sector)
{
	PropagateCorrelations(this, &UpdateLevelMesh::OnFloorTextureChanged, sector);
}
void UpdateLevelMesh::CeilingTextureChanged(sector_t* sector)
{
	PropagateCorrelations(this, &UpdateLevelMesh::OnCeilingTextureChanged, sector);
}

void UpdateLevelMesh::SectorChangedOther(sector_t* sector)
{
	PropagateCorrelations(this, &UpdateLevelMesh::OnSectorChangedOther, sector);
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
	PropagateCorrelations(this, &UpdateLevelMesh::OnSectorLightChanged, sector);
}

void UpdateLevelMesh::SectorLightThinkerCreated(sector_t* sector, DLighting* lightthinker)
{
	PropagateCorrelations(this, &UpdateLevelMesh::OnSectorLightThinkerCreated, sector, lightthinker);
}

void UpdateLevelMesh::SectorLightThinkerDestroyed(sector_t* sector, DLighting* lightthinker)
{
	PropagateCorrelations(this, &UpdateLevelMesh::OnSectorLightThinkerDestroyed, sector, lightthinker);
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
