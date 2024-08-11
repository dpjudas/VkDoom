
#pragma once

#include "hw_levelmesh.h"
#include "levelmeshhelper.h"
#include "scene/hw_drawstructs.h"
#include "common/rendering/hwrenderer/data/hw_meshbuilder.h"
#include "tarray.h"
#include "vectors.h"
#include "r_defs.h"
#include "bounds.h"
#include <set>
#include <map>

struct FLevelLocals;
struct FPolyObj;
struct HWWallDispatcher;
class DoomLevelMesh;
class MeshBuilder;

struct DoomLevelMeshSurface : public LevelMeshSurface
{
	DoomLevelMeshSurfaceType Type = ST_NONE;
	int TypeIndex = 0;

	subsector_t* Subsector = nullptr;
	side_t* Side = nullptr;
	sector_t* ControlSector = nullptr;

	int PipelineID = 0;

	int NextSurface = -1;
};

struct SideSurfaceBlock
{
	int FirstSurface = -1;
};

struct FlatSurfaceBlock
{
	int FirstSurface = -1;
};

class DoomLevelMesh : public LevelMesh, public UpdateLevelMesh
{
public:
	DoomLevelMesh(FLevelLocals &doomMap);

	LevelMeshSurface* GetSurface(int index) override { return &Surfaces[index]; }
	unsigned int GetSurfaceIndex(const LevelMeshSurface* surface) const override { return (unsigned int)(ptrdiff_t)(static_cast<const DoomLevelMeshSurface*>(surface) - Surfaces.Data()); }
	int GetSurfaceCount() override { return Surfaces.Size(); }

	void BeginFrame(FLevelLocals& doomMap);
	bool TraceSky(const FVector3& start, FVector3 direction, float dist);
	void DumpMesh(const FString& objFilename, const FString& mtlFilename) const;

	void BuildSectorGroups(const FLevelLocals& doomMap);

	TArray<DoomLevelMeshSurface> Surfaces;
	TArray<std::unique_ptr<DoomLevelMeshSurface* []>> PolyLMSurfaces;
	TArray<HWWall> WallPortals;

	TArray<int> sectorGroup; // index is sector, value is sectorGroup
	TArray<int> sectorPortals[2]; // index is sector+plane, value is index into the portal list
	TArray<int> linePortals; // index is linedef, value is index into the portal list

	void CreateLights(FLevelLocals& doomMap);

	void FloorHeightChanged(struct sector_t* sector) override;
	void CeilingHeightChanged(struct sector_t* sector) override;
	void MidTex3DHeightChanged(struct sector_t* sector) override;
	void FloorTextureChanged(struct sector_t* sector) override;
	void CeilingTextureChanged(struct sector_t* sector) override;
	void SectorChangedOther(struct sector_t* sector) override;
	void SideTextureChanged(struct side_t* side, int section) override;
	void SectorLightChanged(struct sector_t* sector) override;
	void SectorLightThinkerCreated(struct sector_t* sector, class DLighting* lightthinker) override;
	void SectorLightThinkerDestroyed(struct sector_t* sector, class DLighting* lightthinker) override;

private:
	void CreateSurfaces(FLevelLocals& doomMap);

	void UpdateSide(FLevelLocals& doomMap, unsigned int sideIndex);
	void UpdateFlat(FLevelLocals& doomMap, unsigned int sectorIndex);

	void FreeSide(FLevelLocals& doomMap, unsigned int sideIndex);
	void FreeFlat(FLevelLocals& doomMap, unsigned int sectorIndex);

	void SetSubsectorLightmap(DoomLevelMeshSurface* surface);
	void SetSideLightmap(DoomLevelMeshSurface* surface);

	void SortIndexes();

	void CreateWallSurface(side_t* side, HWWallDispatcher& disp, MeshBuilder& state, TArray<HWWall>& list, bool isPortal, bool translucent, unsigned int sectorIndex);
	void CreateFlatSurface(HWFlatDispatcher& disp, MeshBuilder& state, TArray<HWFlat>& list, bool isSky, bool translucent, unsigned int sectorIndex);

	void LinkSurfaces(FLevelLocals& doomMap);

	BBox GetBoundsFromSurface(const LevelMeshSurface& surface) const;

	int AddSurfaceToTile(const DoomLevelMeshSurface& surf, uint16_t sampleDimension, bool alwaysUpdate);
	int GetSampleDimension(const DoomLevelMeshSurface& surf, uint16_t sampleDimension);

	void CreatePortals(FLevelLocals& doomMap);
	std::pair<FLightNode*, int> GetSurfaceLightNode(const DoomLevelMeshSurface* doomsurf);

	int GetLightIndex(FDynamicLight* light, int portalgroup);

	TArray<SideSurfaceBlock> Sides;
	TArray<FlatSurfaceBlock> Flats;
	std::map<LightmapTileBinding, int> bindings;
	MeshBuilder state;
};
