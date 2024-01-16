
#pragma once

#include "tarray.h"
#include "vectors.h"
#include "hw_collision.h"
#include "flatvertices.h"
#include "hw_levelmeshlight.h"
#include "hw_levelmeshportal.h"
#include "hw_lightmaptile.h"
#include "hw_levelmeshsurface.h"
#include "hw_materialstate.h"
#include "hw_surfaceuniforms.h"
#include <memory>

#include <dp_rect_pack.h>
typedef dp::rect_pack::RectPacker<int> RectPacker;

struct LevelMeshTileStats;

struct LevelSubmeshDrawRange
{
	int PipelineID;
	int Start;
	int Count;
};

class LevelSubmesh
{
public:
	LevelSubmesh();
	virtual ~LevelSubmesh() = default;

	virtual LevelMeshSurface* GetSurface(int index) { return nullptr; }
	virtual unsigned int GetSurfaceIndex(const LevelMeshSurface* surface) const { return 0xffffffff; }
	virtual int GetSurfaceCount() { return 0; }

	struct
	{
		TArray<FFlatVertex> Vertices;
		TArray<uint32_t> Indexes;
		TArray<int> SurfaceIndexes;
		TArray<int> UniformIndexes;
		TArray<SurfaceUniforms> Uniforms;
		TArray<FMaterialState> Materials;
	} Mesh;

	std::unique_ptr<TriangleMeshShape> Collision;

	TArray<LevelSubmeshDrawRange> DrawList;
	TArray<LevelSubmeshDrawRange> PortalList;

	// Lightmap atlas
	int LMTextureCount = 0;
	int LMTextureSize = 1024;
	TArray<uint16_t> LMTextureData;

	uint16_t LightmapSampleDistance = 16;

	TArray<LightmapTile> LightmapTiles;

	uint32_t AtlasPixelCount() const { return uint32_t(LMTextureCount * LMTextureSize * LMTextureSize); }

	void UpdateCollision();
	void GatherTilePixelStats(LevelMeshTileStats& stats);
	void BuildTileSurfaceLists();
	void SetupTileTransforms();
	void PackLightmapAtlas(int lightmapStartIndex);
};

class LevelMesh
{
public:
	LevelMesh();
	virtual ~LevelMesh() = default;

	std::unique_ptr<LevelSubmesh> StaticMesh = std::make_unique<LevelSubmesh>();
	std::unique_ptr<LevelSubmesh> DynamicMesh = std::make_unique<LevelSubmesh>();

	virtual int AddSurfaceLights(const LevelMeshSurface* surface, LevelMeshLight* list, int listMaxSize) { return 0; }

	LevelMeshSurface* Trace(const FVector3& start, FVector3 direction, float maxDist);

	LevelMeshTileStats GatherTilePixelStats();

	// Map defaults
	FVector3 SunDirection = FVector3(0.0f, 0.0f, -1.0f);
	FVector3 SunColor = FVector3(0.0f, 0.0f, 0.0f);

	TArray<LevelMeshPortal> Portals;
};

struct LevelMeshTileStats
{
	struct Stats
	{
		uint32_t total = 0, dirty = 0;
	};

	Stats tiles, pixels;
};
