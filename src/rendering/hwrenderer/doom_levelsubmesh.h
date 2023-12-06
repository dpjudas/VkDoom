
#pragma once

#include "hw_levelmesh.h"
#include "scene/hw_drawstructs.h"
#include "tarray.h"
#include "vectors.h"
#include "r_defs.h"
#include "bounds.h"
#include <dp_rect_pack.h>
#include <set>

typedef dp::rect_pack::RectPacker<int> RectPacker;

struct FLevelLocals;
struct FPolyObj;

struct DoomLevelMeshSurface : public LevelMeshSurface
{
	DoomLevelMeshSurfaceType Type = ST_NONE;
	int TypeIndex = 0;

	subsector_t* Subsector = nullptr;
	side_t* Side = nullptr;
	sector_t* ControlSector = nullptr;

	FFlatVertex* Vertices = nullptr;
	int PipelineID = 0;
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

	// Used by Maploader
	void LinkSurfaces(FLevelLocals& doomMap);
	void PackLightmapAtlas(int lightmapStartIndex);
	void CreatePortals();

	TArray<DoomLevelMeshSurface> Surfaces;
	TArray<int> sectorGroup; // index is sector, value is sectorGroup

	TArray<std::unique_ptr<DoomLevelMeshSurface*[]>> PolyLMSurfaces;

	TArray<HWWall> WallPortals;

private:
	void Reset();
	void BuildSectorGroups(const FLevelLocals& doomMap);

	void CreateStaticSurfaces(FLevelLocals& doomMap);
	void CreateDynamicSurfaces(FLevelLocals& doomMap);

	void SetSubsectorLightmap(DoomLevelMeshSurface* surface);
	void SetSideLightmap(DoomLevelMeshSurface* surface);

	void SetupLightmapUvs(FLevelLocals& doomMap);

	void CreateIndexes();

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

	void BuildSurfaceParams(int lightMapTextureWidth, int lightMapTextureHeight, LevelMeshSurface& surface);

	static bool IsDegenerate(const FVector3& v0, const FVector3& v1, const FVector3& v2);

	static FVector2 ToFVector2(const DVector2& v) { return FVector2((float)v.X, (float)v.Y); }
	static FVector3 ToFVector3(const DVector3& v) { return FVector3((float)v.X, (float)v.Y, (float)v.Z); }
	static FVector4 ToFVector4(const DVector4& v) { return FVector4((float)v.X, (float)v.Y, (float)v.Z, (float)v.W); }
};

static_assert(alignof(FVector2) == alignof(float[2]) && sizeof(FVector2) == sizeof(float) * 2);