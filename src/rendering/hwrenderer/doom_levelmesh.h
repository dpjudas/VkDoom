
#pragma once

#include "hw_levelmesh.h"
#include "scene/hw_drawstructs.h"
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
};

class DoomLevelMesh : public LevelMesh
{
public:
	DoomLevelMesh(FLevelLocals &doomMap);

	LevelMeshSurface* GetSurface(int index) override { return &Surfaces[index]; }
	unsigned int GetSurfaceIndex(const LevelMeshSurface* surface) const override { return (unsigned int)(ptrdiff_t)(static_cast<const DoomLevelMeshSurface*>(surface) - Surfaces.Data()); }
	int GetSurfaceCount() override { return Surfaces.Size(); }
	int AddSurfaceLights(const LevelMeshSurface* surface, LevelMeshLight* list, int listMaxSize) override;

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

private:
	void Reset();

	void CreateSurfaces(FLevelLocals& doomMap);

	void SetSubsectorLightmap(DoomLevelMeshSurface* surface);
	void SetSideLightmap(DoomLevelMeshSurface* surface);

	void SortIndexes();

	void CreateWallSurface(side_t* side, HWWallDispatcher& disp, MeshBuilder& state, std::map<LightmapTileBinding, int>& bindings, TArray<HWWall>& list, bool isSky, bool translucent);
	void CreateFlatSurface(HWFlatDispatcher& disp, MeshBuilder& state, std::map<LightmapTileBinding, int>& bindings, TArray<HWFlat>& list, bool isSky = false);

	void LinkSurfaces(FLevelLocals& doomMap);

	BBox GetBoundsFromSurface(const LevelMeshSurface& surface) const;

	int AddSurfaceToTile(const DoomLevelMeshSurface& surf, std::map<LightmapTileBinding, int>& bindings);
	int GetSampleDimension(const DoomLevelMeshSurface& surf);

	void CreatePortals(FLevelLocals& doomMap);
	FLightNode* GetSurfaceLightNode(const DoomLevelMeshSurface* doomsurf);
};
