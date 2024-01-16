
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

class DoomLevelSubmesh : public LevelSubmesh
{
public:
	DoomLevelSubmesh(DoomLevelMesh* mesh, FLevelLocals& doomMap, bool staticMesh);

	void Update(FLevelLocals& doomMap);

	LevelMeshSurface* GetSurface(int index) override { return &Surfaces[index]; }
	unsigned int GetSurfaceIndex(const LevelMeshSurface* surface) const override { return (unsigned int)(ptrdiff_t)(static_cast<const DoomLevelMeshSurface*>(surface) - Surfaces.Data()); }
	int GetSurfaceCount() override { return Surfaces.Size(); }

	TArray<DoomLevelMeshSurface> Surfaces;
	TArray<std::unique_ptr<DoomLevelMeshSurface*[]>> PolyLMSurfaces;
	TArray<HWWall> Portals;

private:
	void Reset();

	void CreateStaticSurfaces(FLevelLocals& doomMap);
	void CreateDynamicSurfaces(FLevelLocals& doomMap);

	void SetSubsectorLightmap(DoomLevelMeshSurface* surface);
	void SetSideLightmap(DoomLevelMeshSurface* surface);

	void SortIndexes();

	void CreateWallSurface(side_t* side, HWWallDispatcher& disp, MeshBuilder& state, std::map<LightmapTileBinding, int>& bindings, TArray<HWWall>& list, bool isSky, bool translucent);
	void CreateFlatSurface(HWFlatDispatcher& disp, MeshBuilder& state, std::map<LightmapTileBinding, int>& bindings, TArray<HWFlat>& list, bool isSky = false);

	void LinkSurfaces(FLevelLocals& doomMap);

	BBox GetBoundsFromSurface(const LevelMeshSurface& surface) const;

	int AddSurfaceToTile(const DoomLevelMeshSurface& surf, std::map<LightmapTileBinding, int>& bindings);
	int GetSampleDimension(const DoomLevelMeshSurface& surf);

	DoomLevelMesh* LevelMesh = nullptr;
	bool StaticMesh = true;
};

static_assert(alignof(FVector2) == alignof(float[2]) && sizeof(FVector2) == sizeof(float) * 2);
