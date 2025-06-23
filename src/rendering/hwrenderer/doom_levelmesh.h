
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

struct HWMissing
{
	side_t* side;
	subsector_t* sub;
	double plane;
};

struct DoomSurfaceInfo
{
	DoomLevelMeshSurfaceType Type = ST_NONE;
	int TypeIndex = 0;

	subsector_t* Subsector = nullptr;
	side_t* Side = nullptr;
	sector_t* ControlSector = nullptr;

	int LightListSection = 0;

	int NextSurface = -1;
	int NextSubsectorSurface = -1;
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
	LevelMeshDrawType DrawType = {};
	int PipelineID = 0;
	int IndexStart = 0;
	int IndexCount = 0;
};

enum SurfaceUpdateType
{
	None,
	LightLevel,
	Shadows,
	LightList,
	Full
};

struct SideSurfaceBlock
{
	int FirstSurface = -1;
	TArray<GeometryFreeInfo> Geometries;
	TArray<UniformsAllocInfo> Uniforms;
	TArray<HWWall> WallPortals;
	TArray<HWMissing> MissingUpper;
	TArray<HWMissing> MissingLower;
	TArray<HWDecalCreateInfo> Decals;
	bool InSideDecalsList = false;
	bool NeedsImmediateRendering = false;
	TArray<DrawRangeInfo> DrawRanges;
	SurfaceUpdateType UpdateType = SurfaceUpdateType::None;
	LightListAllocInfo Lights;
	TArray<seg_t*> PolySegs;
};

struct FlatSurfaceBlock
{
	int FirstSurface = -1;
	TArray<GeometryFreeInfo> Geometries;
	TArray<UniformsAllocInfo> Uniforms;
	TArray<DrawRangeInfo> DrawRanges;
	SurfaceUpdateType UpdateType = SurfaceUpdateType::None;
	TArray<LightListAllocInfo> Lights;
};

enum class LevelMeshDrawType
{
	Opaque,
	Masked,
	MaskedOffset,
	Portal,
	Translucent,
	TranslucentBorder,
	NumDrawTypes
};

class LevelMeshDrawLists
{
public:
	TArray<TArray<MeshBufferRange>> List[static_cast<int>(LevelMeshDrawType::NumDrawTypes)];

	void Clear()
	{
		for (auto& l : List)
		{
			for (auto& p : l)
			{
				p.Clear();
			}
		}
	}

	void Add(LevelMeshDrawType drawType, int pipelineID, const MeshBufferRange& range)
	{
		int listIndex = static_cast<int>(drawType);
		if (pipelineID >= (int)List[listIndex].Size())
			List[listIndex].Resize(pipelineID + 1);
		List[listIndex][pipelineID].Push(range);
	}
};

class DoomLevelMesh : public LevelMesh, public UpdateLevelMesh
{
public:
	DoomLevelMesh(FLevelLocals &doomMap);
	~DoomLevelMesh();

	void FullRefresh() override
	{
		for (unsigned int i = 0; i < Sides.Size(); i++)
			UpdateSide(i, SurfaceUpdateType::Full);
		for (unsigned int i = 0; i < Flats.Size(); i++)
			UpdateFlat(i, SurfaceUpdateType::Full);
	}

	void PrintSurfaceInfo(const LevelMeshSurface* surface);

	void BeginFrame(FLevelLocals& doomMap);
	bool TraceSky(const FVector3& start, FVector3 direction, float dist);
	void DumpMesh(const FString& objFilename, const FString& mtlFilename) const;

	void BuildSectorGroups(const FLevelLocals& doomMap);

	void AddSectorsToDrawLists(const TArray<int>& sectors, LevelMeshDrawLists& lists);
	void AddSidesToDrawLists(const TArray<int>& sides, LevelMeshDrawLists& lists, HWDrawInfo* di, FRenderState& state);

	TArray<HWWall>& GetSidePortals(int sideIndex);

	TArray<int> sectorGroup; // index is sector, value is sectorGroup
	TArray<int> sectorPortals[2]; // index is sector+plane, value is index into the portal list
	TArray<int> linePortals; // index is linedef, value is index into the portal list

	void SaveLightmapLump(FLevelLocals& doomMap);
	void DeleteLightmapLump(FLevelLocals& doomMap);
	static FString GetMapFilename(FLevelLocals& doomMap);

	void OnFloorHeightChanged(sector_t* sector) override;
	void OnCeilingHeightChanged(sector_t* sector) override;
	void OnMidTex3DHeightChanged(sector_t* sector) override;
	void OnFloorTextureChanged(sector_t* sector) override;
	void OnCeilingTextureChanged(sector_t* sector) override;
	void OnSectorChangedTexZ(sector_t* sector) override;
	void OnSideTextureChanged(side_t* side, int section) override;
	void OnSideDecalsChanged(side_t* side) override;
	void OnSectorLightChanged(sector_t* sector) override;
	void OnSectorLightThinkerCreated(sector_t* sector, DLighting* lightthinker) override;
	void OnSectorLightThinkerDestroyed(sector_t* sector, DLighting* lightthinker) override;
	void OnSectorLightListChanged(sector_t* sector) override;
	void OnSideLightListChanged(side_t* side) override;

	void Reset() override
	{
		LevelMesh::Reset();
		DoomSurfaceInfos.Clear();
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

	void UpdateLightShadows(sector_t* sector);
	void UpdateLightShadows(FDynamicLight* light);

	void UpdateSide(unsigned int sideIndex, SurfaceUpdateType updateType);
	void UpdateFlat(unsigned int sectorIndex, SurfaceUpdateType updateType);

	void UpdateSideShadows(FLevelLocals& doomMap, unsigned int sideIndex);
	void UpdateFlatShadows(FLevelLocals& doomMap, unsigned int sectorIndex);

	void UpdateSideLightList(FLevelLocals& doomMap, unsigned int sideIndex);
	void UpdateFlatLightList(FLevelLocals& doomMap, unsigned int sectorIndex);

	void CreateSide(FLevelLocals& doomMap, unsigned int sideIndex);
	void CreateFlat(FLevelLocals& doomMap, unsigned int sectorIndex);

	void SetSideLights(FLevelLocals& doomMap, unsigned int sideIndex);
	void SetFlatLights(FLevelLocals& doomMap, unsigned int sectorIndex);

	void FreeSide(FLevelLocals& doomMap, unsigned int sideIndex);
	void FreeFlat(FLevelLocals& doomMap, unsigned int sectorIndex);

	void SetSubsectorLightmap(int surfaceIndex);
	void SetSideLightmap(int surfaceIndex);

	void CreateModelSurfaces(AActor* thing, FSpriteModelFrame* modelframe);

	void CreateWallSurface(side_t* side, HWWallDispatcher& disp, MeshBuilder& state, TArray<HWWall>& list, LevelMeshDrawType drawType, unsigned int sectorIndex, const LightListAllocInfo& lightlist);
	void CreateFlatSurface(HWFlatDispatcher& disp, MeshBuilder& state, TArray<HWFlat>& list, LevelMeshDrawType drawType, bool translucent, unsigned int sectorIndex, const LightListAllocInfo& lightlist, int lightlistSection);

	BBox GetBoundsFromSurface(const LevelMeshSurface& surface) const;

	int AddSurfaceToTile(const DoomSurfaceInfo& info, const LevelMeshSurface& surf, uint16_t sampleDimension, uint8_t alwaysUpdate);
	int GetSampleDimension(uint16_t sampleDimension);

	void CreatePortals(FLevelLocals& doomMap);

	LightListAllocInfo CreateLightList(FLightNode* node, int portalgroup);
	int GetLightIndex(FDynamicLight* light, int portalgroup);
	void UpdateLight(FDynamicLight* light);
	void CopyToMeshLight(FDynamicLight* light, LevelMeshLight& meshlight, int portalgroup);

	void AddToDrawList(TArray<DrawRangeInfo>& drawRanges, LevelMeshDrawType drawType, int pipelineID, int indexStart, int indexCount);

	void UploadDynLights(FLevelLocals& doomMap);

	void ReleaseTiles(int surfaceIndex);

	void BuildSideVisibilityLists(FLevelLocals& doomMap);
	void BuildSubsectorVisibilityLists(FLevelLocals& doomMap);

	DoomSurfaceInfo* GetDoomSurface(const SurfaceAllocInfo& sinfo)
	{
		size_t i = (size_t)sinfo.Index;
		if (DoomSurfaceInfos.size() <= i)
			DoomSurfaceInfos.resize(i + 1);
		return &DoomSurfaceInfos[i];
	}

	TArray<DoomSurfaceInfo> DoomSurfaceInfos;

	TArray<SideSurfaceBlock> Sides;
	TArray<FlatSurfaceBlock> Flats;
	TArray<int> SubsectorSurfaces;
	TArray<side_t*> PolySides;

	TArray<int> SideUpdateList;
	TArray<int> FlatUpdateList;

	TArray<TArray<int>> VisibleSides;
	TArray<TArray<int>> VisibleSubsectors;

	std::map<LightmapTileBinding, int> TileBindings;
	MeshBuilder state;
};
