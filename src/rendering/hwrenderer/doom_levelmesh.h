
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

struct DoomLevelMeshSurface : public LevelMeshSurface
{
	subsector_t* Subsector = nullptr;
	side_t* Side = nullptr;
	sector_t* ControlSector = nullptr;
	float* TexCoords;
};

class DoomLevelSubmesh : public LevelSubmesh
{
public:
	void CreateStatic(FLevelLocals& doomMap);
	void CreateDynamic(FLevelLocals& doomMap);

	LevelMeshSurface* GetSurface(int index) override { return &Surfaces[index]; }
	unsigned int GetSurfaceIndex(const LevelMeshSurface* surface) const override { return (unsigned int)(ptrdiff_t)(static_cast<const DoomLevelMeshSurface*>(surface) - Surfaces.Data()); }
	int GetSurfaceCount() override { return Surfaces.Size(); }

	void DumpMesh(const FString& objFilename, const FString& mtlFilename) const;

	// Used by Maploader
	void BindLightmapSurfacesToGeometry(FLevelLocals& doomMap);
	void PackLightmapAtlas();
	void CreatePortals();
	void DisableLightmaps() { Surfaces.Clear(); } // Temp hack that disables lightmapping

	TArray<DoomLevelMeshSurface> Surfaces;
	TArray<FVector2> LightmapUvs;
	TArray<int> sectorGroup; // index is sector, value is sectorGroup

private:
	void BuildSectorGroups(const FLevelLocals& doomMap);

	void CreateSubsectorSurfaces(FLevelLocals& doomMap);
	void CreateCeilingSurface(FLevelLocals& doomMap, subsector_t* sub, sector_t* sector, sector_t* controlSector, int typeIndex);
	void CreateFloorSurface(FLevelLocals& doomMap, subsector_t* sub, sector_t* sector, sector_t* controlSector, int typeIndex);
	void CreateSideSurfaces(FLevelLocals& doomMap, side_t* side);

	void CreateSurfaceTextureUVs(FLevelLocals& doomMap);

	void SetSubsectorLightmap(DoomLevelMeshSurface* surface);
	void SetSideLightmap(DoomLevelMeshSurface* surface);

	void SetupLightmapUvs(FLevelLocals& doomMap);

	static bool IsTopSideSky(sector_t* frontsector, sector_t* backsector, side_t* side);
	static bool IsTopSideVisible(side_t* side);
	static bool IsBottomSideVisible(side_t* side);
	static bool IsSkySector(sector_t* sector, int plane);
	static bool IsControlSector(sector_t* sector);

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
	void FinishSurface(int lightmapTextureWidth, int lightmapTextureHeight, RectPacker& packer, LevelMeshSurface& surface);

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
	
	bool TraceSky(const FVector3& start, FVector3 direction, float dist)
	{
		FVector3 end = start + direction * dist;
		auto surface = Trace(start, direction, dist);
		return surface && surface->bSky;
	}

	int AddSurfaceLights(const LevelMeshSurface* surface, LevelMeshLight* list, int listMaxSize) override;

	void PackLightmapAtlas()
	{
		static_cast<DoomLevelSubmesh*>(StaticMesh.get())->PackLightmapAtlas();
	}

	void BindLightmapSurfacesToGeometry(FLevelLocals& doomMap)
	{
		static_cast<DoomLevelSubmesh*>(StaticMesh.get())->BindLightmapSurfacesToGeometry(doomMap);

		// Runtime helper
		for (auto& surface : static_cast<DoomLevelSubmesh*>(StaticMesh.get())->Surfaces)
		{
			if (surface.ControlSector)
			{
				if (surface.Type == ST_FLOOR || surface.Type == ST_CEILING)
				{
					XFloorToSurface[surface.Subsector->sector].Push(&surface);
				}
				else if (surface.Type == ST_MIDDLESIDE)
				{
					XFloorToSurfaceSides[surface.ControlSector].Push(&surface);
				}
			}
		}
	}

	void DisableLightmaps()
	{
		static_cast<DoomLevelSubmesh*>(StaticMesh.get())->DisableLightmaps();
	}

	void DumpMesh(const FString& objFilename, const FString& mtlFilename) const
	{
		static_cast<DoomLevelSubmesh*>(StaticMesh.get())->DumpMesh(objFilename, mtlFilename);
	}

	// To do: remove these. Use ffloors on flats and sides to find the 3d surfaces as that is both faster and culls better
	TMap<const sector_t*, TArray<DoomLevelMeshSurface*>> XFloorToSurface;
	TMap<const sector_t*, TArray<DoomLevelMeshSurface*>> XFloorToSurfaceSides;
};
