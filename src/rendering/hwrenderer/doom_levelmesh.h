
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
struct HWDrawInfo;
class DoomLevelMesh;
class MeshBuilder;

struct DoomSurfaceInfo
{
	DoomLevelMeshSurfaceType Type = ST_NONE;
	int TypeIndex = 0;

	subsector_t* Subsector = nullptr;
	side_t* Side = nullptr;
	sector_t* ControlSector = nullptr;

	int NextSurface = -1;
};

struct GeometryFreeInfo
{
	GeometryFreeInfo(const GeometryAllocInfo& ginfo)
	{
		VertexStart = ginfo.VertexStart;
		VertexCount = ginfo.VertexCount;
		IndexStart = ginfo.IndexStart;
		IndexCount = ginfo.IndexCount;
	}

	int VertexStart = 0;
	int VertexCount = 0;
	int IndexStart = 0;
	int IndexCount = 0;
};

struct DrawRangeInfo
{
	int PipelineID = 0;
	LevelMeshDrawType DrawType = {};
	int DrawIndexStart = 0;
	int DrawIndexCount = 0;
};

enum SurfaceUpdateType
{
	None,
	LightsOnly,
	Full
};

struct SideSurfaceBlock
{
	int FirstSurface = -1;
	TArray<GeometryFreeInfo> Geometries;
	TArray<UniformsAllocInfo> Uniforms;
	TArray<HWWall> WallPortals;
	TArray<HWDecalCreateInfo> Decals;
	bool InSidePortalsList = false;
	bool InSideDecalsList = false;
	TArray<DrawRangeInfo> DrawRanges;
	SurfaceUpdateType UpdateType = SurfaceUpdateType::None;
};

struct FlatSurfaceBlock
{
	int FirstSurface = -1;
	TArray<GeometryFreeInfo> Geometries;
	TArray<UniformsAllocInfo> Uniforms;
	TArray<DrawRangeInfo> DrawRanges;
	SurfaceUpdateType UpdateType = SurfaceUpdateType::None;
};

class DoomLevelMesh : public LevelMesh, public UpdateLevelMesh
{
public:
	DoomLevelMesh(FLevelLocals &doomMap);

	void PrintSurfaceInfo(const LevelMeshSurface* surface);

	void BeginFrame(FLevelLocals& doomMap);
	bool TraceSky(const FVector3& start, FVector3 direction, float dist);
	void DumpMesh(const FString& objFilename, const FString& mtlFilename) const;

	void BuildSectorGroups(const FLevelLocals& doomMap);

	void ProcessDecals(HWDrawInfo* drawinfo, FRenderState& state);

	TArray<int> SideDecals;
	TArray<int> SidePortals;
	TArray<HWWall*> WallPortals;

	TArray<int> sectorGroup; // index is sector, value is sectorGroup
	TArray<int> sectorPortals[2]; // index is sector+plane, value is index into the portal list
	TArray<int> linePortals; // index is linedef, value is index into the portal list

	void CreateLights(FLevelLocals& doomMap);

	void SaveLightmapLump(FLevelLocals& doomMap);
	void DeleteLightmapLump(FLevelLocals& doomMap);
	static FString GetMapFilename(FLevelLocals& doomMap);

	void OnFloorHeightChanged(sector_t* sector) override;
	void OnCeilingHeightChanged(sector_t* sector) override;
	void OnMidTex3DHeightChanged(sector_t* sector) override;
	void OnFloorTextureChanged(sector_t* sector) override;
	void OnCeilingTextureChanged(sector_t* sector) override;
	void OnSectorChangedOther(sector_t* sector) override;
	void OnSideTextureChanged(side_t* side, int section) override;
	void OnSideDecalsChanged(side_t* side) override;
	void OnSectorLightChanged(sector_t* sector) override;
	void OnSectorLightThinkerCreated(sector_t* sector, DLighting* lightthinker) override;
	void OnSectorLightThinkerDestroyed(sector_t* sector, DLighting* lightthinker) override;

	void Reset(const LevelMeshLimits& limits) override
	{
		LevelMesh::Reset(limits);
		DoomSurfaceInfos.Resize(limits.MaxSurfaces);
	}

	void GetVisibleSurfaces(LightmapTile* tile, TArray<int>& outSurfaces) override;

	struct Stats
	{
		int FlatsUpdated = 0;
		int SidesUpdated = 0;
		int Portals = 0;
		int DynLights = 0;
	};
	Stats LastFrameStats, CurFrameStats;

private:
	void SetLimits(FLevelLocals& doomMap);

	void CreateSurfaces(FLevelLocals& doomMap);
	void CreateLightList(FLevelLocals& doomMap, int surfaceIndex);

	void UpdateSide(unsigned int sideIndex, SurfaceUpdateType updateType);
	void UpdateFlat(unsigned int sectorIndex, SurfaceUpdateType updateType);

	void CreateSide(FLevelLocals& doomMap, unsigned int sideIndex);
	void CreateFlat(FLevelLocals& doomMap, unsigned int sectorIndex);

	void SetSideLights(FLevelLocals& doomMap, unsigned int sideIndex);
	void SetFlatLights(FLevelLocals& doomMap, unsigned int sectorIndex);

	void FreeSide(FLevelLocals& doomMap, unsigned int sideIndex);
	void FreeFlat(FLevelLocals& doomMap, unsigned int sectorIndex);

	void UpdateWallPortals();

	void SetSubsectorLightmap(int surfaceIndex);
	void SetSideLightmap(int surfaceIndex);

	void CreateWallSurface(side_t* side, HWWallDispatcher& disp, MeshBuilder& state, TArray<HWWall>& list, LevelMeshDrawType drawType, bool translucent, unsigned int sectorIndex);
	void CreateFlatSurface(HWFlatDispatcher& disp, MeshBuilder& state, TArray<HWFlat>& list, LevelMeshDrawType drawType, bool translucent, unsigned int sectorIndex);

	BBox GetBoundsFromSurface(const LevelMeshSurface& surface) const;

	int AddSurfaceToTile(const DoomSurfaceInfo& info, const LevelMeshSurface& surf, uint16_t sampleDimension, bool alwaysUpdate);
	int GetSampleDimension(uint16_t sampleDimension);

	void CreatePortals(FLevelLocals& doomMap);
	std::pair<FLightNode*, int> GetSurfaceLightNode(int surfaceIndex);

	int GetLightIndex(FDynamicLight* light, int portalgroup);

	void AddToDrawList(TArray<DrawRangeInfo>& drawRanges, int pipelineID, int indexStart, int indexCount, LevelMeshDrawType drawType);
	void RemoveFromDrawList(const TArray<DrawRangeInfo>& drawRanges);
	void SortDrawLists();

	void UploadDynLights(FLevelLocals& doomMap);

	TArray<DoomSurfaceInfo> DoomSurfaceInfos;

	TArray<SideSurfaceBlock> Sides;
	TArray<FlatSurfaceBlock> Flats;
	TArray<side_t*> PolySides;

	TArray<int> SideUpdateList;
	TArray<int> FlatUpdateList;

	std::map<LightmapTileBinding, int> bindings;
	MeshBuilder state;
	bool LightsCreated = false;
};
