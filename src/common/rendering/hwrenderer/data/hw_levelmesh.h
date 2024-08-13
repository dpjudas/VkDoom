
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

struct GeometryAllocInfo
{
	FFlatVertex* Vertices = nullptr;
	int* UniformIndexes = nullptr;
	uint32_t* Indexes = nullptr;
	int VertexStart = 0;
	int VertexCount = 0;
	int IndexStart = 0;
	int IndexCount = 0;
};

struct UniformsAllocInfo
{
	SurfaceUniforms* Uniforms = nullptr;
	FMaterialState* Materials = nullptr;
	int Start = 0;
	int Count = 0;
};

struct MeshBufferRange
{
	int Offset = 0;
	int Size = 0;

	bool operator<(const MeshBufferRange& other) const { return Offset < other.Offset; }
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

	GeometryAllocInfo AllocGeometry(int vertexCount, int indexCount)
	{
		GeometryAllocInfo info;
		info.VertexStart = Mesh.Vertices.Size();
		info.VertexCount = vertexCount;
		info.IndexStart = Mesh.Indexes.Size();
		info.IndexCount = indexCount;
		Mesh.Vertices.Resize(info.VertexStart + vertexCount);
		Mesh.UniformIndexes.Resize(info.VertexStart + vertexCount);
		Mesh.Indexes.Resize(info.IndexStart + indexCount);
		info.Vertices = &Mesh.Vertices[info.VertexStart];
		info.UniformIndexes = &Mesh.UniformIndexes[info.VertexStart];
		info.Indexes = &Mesh.Indexes[info.IndexStart];

		AddRange(UploadRanges.Vertex, { info.VertexStart, info.VertexCount });
		AddRange(UploadRanges.UniformIndexes, { info.VertexStart, info.VertexCount });
		AddRange(UploadRanges.Index, { info.IndexStart, info.IndexCount });

		return info;
	}

	UniformsAllocInfo AllocUniforms(int count)
	{
		UniformsAllocInfo info;
		info.Start = Mesh.Uniforms.Size();
		info.Count = count;
		Mesh.Uniforms.Resize(info.Start + count);
		Mesh.Materials.Resize(info.Start + count);
		info.Uniforms = &Mesh.Uniforms[info.Start];
		info.Materials = &Mesh.Materials[info.Start];

		AddRange(UploadRanges.Uniforms, { info.Start, info.Count });

		return info;
	}

	void FreeGeometry(int vertexStart, int vertexCount, int indexStart, int indexCount)
	{
		// Convert triangles to degenerates
		for (int i = 0; i < indexCount; i++)
			Mesh.Indexes[indexStart + i] = 0;
		AddRange(UploadRanges.Index, { indexStart, indexCount });

		AddRange(FreeLists.Vertex, { vertexStart, vertexCount });
		AddRange(FreeLists.Index, { indexStart, indexCount });
	}

	void FreeUniforms(int start, int count)
	{
		AddRange(FreeLists.Uniforms, { start, count });
	}

	void FreeSurface(unsigned int surfaceIndex)
	{
		// To do: remove the surface from the surface tile, if attached

		AddRange(FreeLists.Surface, { (int)surfaceIndex, 1 });
	}

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

		// Above data must not be resized beyond these limits as that's the size of the GPU buffers
		int MaxVertices = 0;
		int MaxIndexes = 0;
		int MaxSurfaces = 0;
		int MaxUniforms = 0;
		int MaxSurfaceIndexes = 0;
		int MaxNodes = 0;
		int MaxLights = 0;
		int MaxLightIndexes = 0;
	} Mesh;

	struct
	{
		TArray<MeshBufferRange> Vertex;
		TArray<MeshBufferRange> Index;
		TArray<MeshBufferRange> Node;
		TArray<MeshBufferRange> SurfaceIndex;
		TArray<MeshBufferRange> Surface;
		TArray<MeshBufferRange> UniformIndexes;
		TArray<MeshBufferRange> Uniforms;
		TArray<MeshBufferRange> Portals;
		TArray<MeshBufferRange> Light;
		TArray<MeshBufferRange> LightIndex;
	} UploadRanges;

	struct
	{
		TArray<MeshBufferRange> Vertex;
		TArray<MeshBufferRange> Index;
		TArray<MeshBufferRange> Uniforms;
		TArray<MeshBufferRange> Surface;
	} FreeLists;

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
	
	void UploadAll()
	{
		ClearRanges(UploadRanges.Vertex);
		AddRange(UploadRanges.Vertex, { 0, (int)Mesh.Vertices.Size() });
		ClearRanges(UploadRanges.Index);
		AddRange(UploadRanges.Index, { 0, (int)Mesh.Indexes.Size() });
		ClearRanges(UploadRanges.SurfaceIndex);
		AddRange(UploadRanges.SurfaceIndex, { 0, (int)Mesh.SurfaceIndexes.Size() });
		ClearRanges(UploadRanges.Surface);
		AddRange(UploadRanges.Surface, { 0, GetSurfaceCount() });
		ClearRanges(UploadRanges.UniformIndexes);
		AddRange(UploadRanges.UniformIndexes, { 0, (int)Mesh.UniformIndexes.Size() });
		ClearRanges(UploadRanges.Uniforms);
		AddRange(UploadRanges.Uniforms, { 0, (int)Mesh.Uniforms.Size() });
		ClearRanges(UploadRanges.Portals);
		AddRange(UploadRanges.Portals, { 0, (int)Portals.Size() });
		ClearRanges(UploadRanges.Light);
		AddRange(UploadRanges.Light, { 0, (int)Mesh.Lights.Size() });
		ClearRanges(UploadRanges.LightIndex);
		AddRange(UploadRanges.LightIndex, { 0, (int)Mesh.LightIndexes.Size() });
	}

	void UploadCollision()
	{
		UploadRanges.Node.Clear();
		if (Collision)
			UploadRanges.Node.Push({ 0, (int)Collision->get_nodes().size() });
	}

	void ClearRanges(TArray<MeshBufferRange>& ranges)
	{
		ranges.Clear();
	}

	void AddRange(TArray<MeshBufferRange>& ranges, MeshBufferRange range)
	{
		if (range.Size <= 0)
			return;

		auto right = std::lower_bound(ranges.begin(), ranges.end(), range);

		bool leftExists = right != ranges.begin();
		bool rightExists = right != ranges.end();

		auto left = right;
		if (leftExists)
			--left;

		if (leftExists && rightExists && left->Offset + left->Size == range.Offset && right->Offset == range.Offset + range.Size) // ####[--]####
		{
			left->Size += range.Size + right->Size;
			ranges.Delete(right - ranges.begin());
		}
		else if (leftExists && left->Offset + left->Size == range.Offset) // ####[--]  ####
		{
			left->Size += range.Size;
		}
		else if (rightExists && right->Offset == range.Offset + range.Size) // ####  [--]####
		{
			right->Offset -= range.Size;
		}
		else // ##### [--]  ####
		{
			ranges.Insert(right - ranges.begin(), range);
		}
	}
};

struct LevelMeshTileStats
{
	struct Stats
	{
		uint32_t total = 0, dirty = 0;
	};

	Stats tiles, pixels;
};
