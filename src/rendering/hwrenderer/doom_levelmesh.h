
#pragma once

#include "hw_levelmesh.h"
#include "tarray.h"
#include "vectors.h"
#include "r_defs.h"
#include "bounds.h"
#include <dp_rect_pack.h>
#include <set>

typedef dp::rect_pack::RectPacker<int> RectPacker;

struct FLevelLocals;
struct FPolyObj;

enum DoomLevelMeshSurfaceType
{
	ST_UNKNOWN,
	ST_MIDDLESIDE,
	ST_UPPERSIDE,
	ST_LOWERSIDE,
	ST_CEILING,
	ST_FLOOR
};

struct DoomLevelMeshSurface : public LevelMeshSurface
{
	DoomLevelMeshSurfaceType Type = ST_UNKNOWN;
	int TypeIndex = 0;

	subsector_t* Subsector = nullptr;
	side_t* Side = nullptr;
	sector_t* ControlSector = nullptr;

	float* TexCoords = nullptr;
};

class DoomLevelSubmesh : public LevelSubmesh
{
public:
	void CreateStatic(FLevelLocals& doomMap);
	void CreateDynamic(FLevelLocals& doomMap);
	void UpdateDynamic(FLevelLocals& doomMap, int lightmapStartIndex);

	LevelMeshSurface* GetSurface(int index) override { return &Surfaces[index]; }
	unsigned int GetSurfaceIndex(const LevelMeshSurface* surface) const override { return (unsigned int)(ptrdiff_t)(static_cast<const DoomLevelMeshSurface*>(surface) - Surfaces.Data()); }
	int GetSurfaceCount() override { return Surfaces.Size(); }

	void DumpMesh(const FString& objFilename, const FString& mtlFilename) const;

	// Used by Maploader
	void BindLightmapSurfacesToGeometry(FLevelLocals& doomMap);
	void PackLightmapAtlas(int lightmapStartIndex);
	void CreatePortals();
	void DisableLightmaps() { Surfaces.Clear(); } // Temp hack that disables lightmapping

	TArray<DoomLevelMeshSurface> Surfaces;
	TArray<FVector2> LightmapUvs;
	TArray<int> sectorGroup; // index is sector, value is sectorGroup

	TArray<std::unique_ptr<DoomLevelMeshSurface*[]>> PolyLMSurfaces;

private:
	void BuildSectorGroups(const FLevelLocals& doomMap);

	void CreateSubsectorSurfaces(FLevelLocals& doomMap);
	void CreateCeilingSurface(FLevelLocals& doomMap, subsector_t* sub, sector_t* sector, sector_t* controlSector, int typeIndex);
	void CreateFloorSurface(FLevelLocals& doomMap, subsector_t* sub, sector_t* sector, sector_t* controlSector, int typeIndex);
	void CreateSideSurfaces(FLevelLocals& doomMap, side_t* side);
	void CreateLinePortalSurface(FLevelLocals& doomMap, side_t* side);
	void CreateLineHorizonSurface(FLevelLocals& doomMap, side_t* side);
	void CreateFrontWallSurface(FLevelLocals& doomMap, side_t* side);
	void CreateTopWallSurface(FLevelLocals& doomMap, side_t* side);
	void CreateMidWallSurface(FLevelLocals& doomMap, side_t* side);
	void CreateBottomWallSurface(FLevelLocals& doomMap, side_t* side);
	void Create3DFloorWallSurfaces(FLevelLocals& doomMap, side_t* side);
	void SetSideTextureUVs(DoomLevelMeshSurface& surface, side_t* side, side_t::ETexpart texpart, float v1TopZ, float v1BottomZ, float v2TopZ, float v2BottomZ);

	void SetSubsectorLightmap(DoomLevelMeshSurface* surface);
	void SetSideLightmap(DoomLevelMeshSurface* surface);

	void SetupLightmapUvs(FLevelLocals& doomMap);

	void CreateIndexes();

	static bool IsTopSideSky(sector_t* frontsector, sector_t* backsector, side_t* side);
	static bool IsTopSideVisible(side_t* side);
	static bool IsBottomSideVisible(side_t* side);
	static bool IsSkySector(sector_t* sector, int plane);

	static FVector4 ToPlane(const FVector3& pt1, const FVector3& pt2, const FVector3& pt3)
	{
		FVector3 n = ((pt2 - pt1) ^ (pt3 - pt2)).Unit();
		float d = pt1 | n;
		return FVector4(n.X, n.Y, n.Z, d);
	}

	static FVector4 ToPlane(const FVector3& pt1, const FVector3& pt2, const FVector3& pt3, const FVector3& pt4)
	{
		if (pt1.ApproximatelyEquals(pt3))
		{
			return ToPlane(pt1, pt2, pt4);
		}
		else if (pt1.ApproximatelyEquals(pt2) || pt2.ApproximatelyEquals(pt3))
		{
			return ToPlane(pt1, pt3, pt4);
		}

		return ToPlane(pt1, pt2, pt3);
	}

	// Lightmapper

	enum PlaneAxis
	{
		AXIS_YZ = 0,
		AXIS_XZ,
		AXIS_XY
	};

	static PlaneAxis BestAxis(const FVector4& p);
	BBox GetBoundsFromSurface(const LevelMeshSurface& surface) const;

	inline int AllocUvs(int amount) { return LightmapUvs.Reserve(amount); }

	void BuildSurfaceParams(int lightMapTextureWidth, int lightMapTextureHeight, LevelMeshSurface& surface);

	static bool IsDegenerate(const FVector3& v0, const FVector3& v1, const FVector3& v2);

	static FVector2 ToFVector2(const DVector2& v) { return FVector2((float)v.X, (float)v.Y); }
	static FVector3 ToFVector3(const DVector3& v) { return FVector3((float)v.X, (float)v.Y, (float)v.Z); }
	static FVector4 ToFVector4(const DVector4& v) { return FVector4((float)v.X, (float)v.Y, (float)v.Z, (float)v.W); }
};

static_assert(alignof(FVector2) == alignof(float[2]) && sizeof(FVector2) == sizeof(float) * 2);

class DoomLevelMesh : public LevelMesh
{
public:
	DoomLevelMesh(FLevelLocals &doomMap);

	int AddSurfaceLights(const LevelMeshSurface* surface, LevelMeshLight* list, int listMaxSize) override;

	void BeginFrame(FLevelLocals& doomMap);
	bool TraceSky(const FVector3& start, FVector3 direction, float dist);
	void PackLightmapAtlas();
	void BindLightmapSurfacesToGeometry(FLevelLocals& doomMap);
	void DisableLightmaps();
	void DumpMesh(const FString& objFilename, const FString& mtlFilename) const;
};
