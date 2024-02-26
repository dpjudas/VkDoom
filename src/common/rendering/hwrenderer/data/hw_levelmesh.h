
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

class LevelMesh
{
public:
	LevelMesh();
	virtual ~LevelMesh() = default;

	virtual LevelMeshSurface* GetSurface(int index) { return nullptr; }
	virtual unsigned int GetSurfaceIndex(const LevelMeshSurface* surface) const { return 0xffffffff; }
	virtual int GetSurfaceCount() { return 0; }

	LevelMeshSurface* Trace(const FVector3& start, FVector3 direction, float maxDist);

	LevelMeshTileStats GatherTilePixelStats();

	// Map defaults
	FVector3 SunDirection = FVector3(0.0f, 0.0f, -1.0f);
	FVector3 SunColor = FVector3(0.0f, 0.0f, 0.0f);

	TArray<LevelMeshPortal> Portals;

	struct
	{
		// Vertex data
		TArray<FFlatVertex> Vertices;
		TArray<int> UniformIndexes;

		// Surface info
		TArray<SurfaceUniforms> Uniforms;
		TArray<FMaterialState> Materials;
		TArray<int32_t> LightIndexes;

		// Lights
		TArray<LevelMeshLight> Lights;

		// Index data
		TArray<uint32_t> Indexes;
		TArray<int> SurfaceIndexes;
		int DynamicIndexStart = 0;

		// Above data must not be resized beyond these limits as that's the size of the GPU buffers)
		int MaxVertices = 0;
		int MaxIndexes = 0;
		int MaxSurfaces = 0;
		int MaxUniforms = 0;
		int MaxSurfaceIndexes = 0;
		int MaxNodes = 0;
		int MaxLights = 0;
		int MaxLightIndexes = 0;
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
	void BuildTileSurfaceLists();
	void SetupTileTransforms();
	void PackLightmapAtlas(int lightmapStartIndex);

	void AddEmptyMesh();
};

struct LevelMeshTileStats
{
	struct Stats
	{
		uint32_t total = 0, dirty = 0;
	};

	Stats tiles, pixels;
};
