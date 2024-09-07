
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
#include "engineerrors.h"
#include <memory>
#include <unordered_map>

#include <dp_rect_pack.h>
typedef dp::rect_pack::RectPacker<int> RectPacker;

struct LevelMeshTileStats;

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

struct SurfaceAllocInfo
{
	LevelMeshSurface* Surface = nullptr;
	int Index = 0;
};

struct LightAllocInfo
{
	LevelMeshLight* Light = nullptr;
	int Index = 0;
};

struct MeshBufferRange
{
	int Start = 0;
	int End = 0;

	int Count() const { return End - Start; }
};

struct LevelMeshLimits
{
	int MaxVertices = 0;
	int MaxSurfaces = 0;
	int MaxUniforms = 0;
	int MaxIndexes = 0;
};

enum class LevelMeshDrawType
{
	Opaque,
	Masked,
	Portal,
	Translucent,
	NumDrawTypes
};

class LevelMesh
{
public:
	LevelMesh();
	virtual ~LevelMesh() = default;

	LevelMeshSurface* Trace(const FVector3& start, FVector3 direction, float maxDist);

	LevelMeshTileStats GatherTilePixelStats();

	// Map defaults
	FVector3 SunDirection = FVector3(0.0f, 0.0f, -1.0f);
	FVector3 SunColor = FVector3(0.0f, 0.0f, 0.0f);

	TArray<LevelMeshPortal> Portals;

	GeometryAllocInfo AllocGeometry(int vertexCount, int indexCount);
	UniformsAllocInfo AllocUniforms(int count);
	SurfaceAllocInfo AllocSurface();

	void FreeGeometry(int vertexStart, int vertexCount, int indexStart, int indexCount);
	void FreeUniforms(int start, int count);
	void FreeSurface(unsigned int surfaceIndex);

	// Sets the sizes of all the GPU buffers and empties the mesh
	virtual void Reset(const LevelMeshLimits& limits);

	virtual void GetVisibleSurfaces(LightmapTile* tile, TArray<int>& outSurfaces) { }

	// Data placed in GPU buffers
	struct
	{
		// Vertex data
		TArray<FFlatVertex> Vertices;
		TArray<int> UniformIndexes;

		// Surface info
		TArray<LevelMeshSurface> Surfaces;
		TArray<SurfaceUniforms> Uniforms;
		TArray<FMaterialState> Materials;
		TArray<int32_t> LightIndexes;

		// Lights
		TArray<LevelMeshLight> Lights;

		// Index data
		TArray<uint32_t> Indexes;
		TArray<int> SurfaceIndexes;
		int IndexCount = 0; // Index range filled with data

		// Indexes sorted by pipeline
		TArray<uint32_t> DrawIndexes;

		// GPU buffer size for collision nodes
		int MaxNodes = 0;
	} Mesh;

	// Ranges in mesh that have changed since last upload
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
		TArray<MeshBufferRange> DrawIndex;
	} UploadRanges;

	// Ranges in mesh currently not in use
	struct
	{
		TArray<MeshBufferRange> Vertex;
		TArray<MeshBufferRange> Index;
		TArray<MeshBufferRange> Uniforms;
		TArray<MeshBufferRange> Surface;
		TArray<MeshBufferRange> DrawIndex;
	} FreeLists;

	// Data structure for doing mesh traces on the CPU
	std::unique_ptr<TriangleMeshShape> Collision;

	// Draw index ranges for rendering the level mesh, grouped by pipeline
	std::unordered_map<int, TArray<MeshBufferRange>> DrawList[(int)LevelMeshDrawType::NumDrawTypes];

	// Lightmap atlas
	int LMTextureCount = 0;
	int LMTextureSize = 1024;
	TArray<uint16_t> LMTextureData;

	uint16_t LightmapSampleDistance = 16;

	TArray<LightmapTile> LightmapTiles;
	bool LMAtlasPacked = false; // Tile sizes can't be changed anymore

	uint32_t AtlasPixelCount() const { return uint32_t(LMTextureCount * LMTextureSize * LMTextureSize); }

	void UpdateCollision();
	void BuildTileSurfaceLists();
	void SetupTileTransforms();
	void PackLightmapAtlas(int lightmapStartIndex);

	void AddEmptyMesh();
	
	void UploadPortals();
	void UploadCollision();

	void AddRange(TArray<MeshBufferRange>& ranges, MeshBufferRange range);
	void RemoveRange(TArray<MeshBufferRange>& ranges, MeshBufferRange range);
	int RemoveRange(TArray<MeshBufferRange>& ranges, int count);
};

struct LevelMeshTileStats
{
	struct Stats
	{
		uint32_t total = 0, dirty = 0;
	};

	Stats tiles, pixels;
};

inline GeometryAllocInfo LevelMesh::AllocGeometry(int vertexCount, int indexCount)
{
	GeometryAllocInfo info;
	info.VertexStart = RemoveRange(FreeLists.Vertex, vertexCount);
	info.VertexCount = vertexCount;
	info.IndexStart = RemoveRange(FreeLists.Index, indexCount);
	info.IndexCount = indexCount;
	info.Vertices = &Mesh.Vertices[info.VertexStart];
	info.UniformIndexes = &Mesh.UniformIndexes[info.VertexStart];
	info.Indexes = &Mesh.Indexes[info.IndexStart];

	AddRange(UploadRanges.Vertex, { info.VertexStart, info.VertexStart + info.VertexCount });
	AddRange(UploadRanges.UniformIndexes, { info.VertexStart, info.VertexStart + info.VertexCount });
	AddRange(UploadRanges.Index, { info.IndexStart, info.IndexStart + info.IndexCount });
	AddRange(UploadRanges.SurfaceIndex, { info.IndexStart / 3, (info.IndexStart + info.IndexCount) / 3 });

	Mesh.IndexCount = std::max(Mesh.IndexCount, info.IndexStart + info.IndexCount);

	return info;
}

inline UniformsAllocInfo LevelMesh::AllocUniforms(int count)
{
	UniformsAllocInfo info;
	info.Start = RemoveRange(FreeLists.Uniforms, count);
	info.Count = count;
	info.Uniforms = &Mesh.Uniforms[info.Start];
	info.Materials = &Mesh.Materials[info.Start];

	AddRange(UploadRanges.Uniforms, { info.Start, info.Start + info.Count });

	return info;
}

inline SurfaceAllocInfo LevelMesh::AllocSurface()
{
	SurfaceAllocInfo info;
	info.Index = RemoveRange(FreeLists.Surface, 1);
	info.Surface = &Mesh.Surfaces[info.Index];

	AddRange(UploadRanges.Surface, { info.Index, info.Index + 1 });

	return info;
}

inline void LevelMesh::FreeGeometry(int vertexStart, int vertexCount, int indexStart, int indexCount)
{
	// Convert triangles to degenerates
	for (int i = 0; i < indexCount; i++)
		Mesh.Indexes[indexStart + i] = 0;
	AddRange(UploadRanges.Index, { indexStart, indexStart + indexCount });

	AddRange(FreeLists.Vertex, { vertexStart, vertexStart + vertexCount });
	AddRange(FreeLists.Index, { indexStart, indexStart + indexCount });
}

inline void LevelMesh::FreeUniforms(int start, int count)
{
	AddRange(FreeLists.Uniforms, { start, start + count });
}

inline void LevelMesh::FreeSurface(unsigned int surfaceIndex)
{
	// To do: remove the surface from the surface tile, if attached

	AddRange(FreeLists.Surface, { (int)surfaceIndex, (int)surfaceIndex + 1 });
}

inline void LevelMesh::UploadPortals()
{
	UploadRanges.Portals.Clear();
	AddRange(UploadRanges.Portals, { 0, (int)Portals.Size() });
}

inline void LevelMesh::UploadCollision()
{
	UploadRanges.Node.Clear();
	if (Collision)
		UploadRanges.Node.Push({ 0, (int)Collision->get_nodes().size() });
}

inline void LevelMesh::AddRange(TArray<MeshBufferRange>& ranges, MeshBufferRange range)
{
	// Empty range?
	if (range.Start == range.End)
		return;

	// First element?
	if (ranges.Size() == 0)
	{
		ranges.push_back(range);
		return;
	}

	// Find start position in ranges
	auto right = std::lower_bound(ranges.begin(), ranges.end(), range, [](const auto& a, const auto& b) { return a.Start < b.Start; });
	bool leftExists = right != ranges.begin();
	bool rightExists = right != ranges.end();
	auto left = right;
	if (leftExists)
		--left;

	// Is this a gap between two ranges?
	if ((!leftExists || left->End < range.Start) && (!rightExists || right->Start > range.End))
	{
		ranges.Insert(right - ranges.begin(), range);
		return;
	}

	// Are we extending the left or the right range?
	if (leftExists && range.Start <= left->End)
	{
		left->End = std::max(left->End, range.End);
		right = left;
	}
	else // if (rightExists && right->Start <= range.End)
	{
		right->Start = range.Start;
		right->End = std::max(right->End, range.End);
		left = right;
	}

	// Merge overlaps to the right
	while (true)
	{
		++right;
		if (right == ranges.end() || right->Start > range.End)
			break;
		left->End = std::max(right->End, range.End);
	}

	// Remove ranges now covered by the extended range
	//ranges.erase(++left, right);
	++left;
	auto leftPos = left - ranges.begin();
	auto rightPos = right - ranges.begin();
	ranges.Delete(leftPos, rightPos - leftPos);
}

inline void LevelMesh::RemoveRange(TArray<MeshBufferRange>& ranges, MeshBufferRange range)
{
	if (range.Start == range.End)
		return;

	auto entry = std::lower_bound(ranges.begin(), ranges.end(), range, [](const auto& a, const auto& b) { return a.End < b.End; });
	if (entry->Start == range.Start && entry->End == range.End)
	{
		ranges.Delete(entry - ranges.begin());
	}
	else if (entry->Start == range.Start)
	{
		entry->Start = range.End;
	}
	else if (entry->End == range.End)
	{
		entry->End = range.Start;
	}
	else
	{
		MeshBufferRange split;
		split.Start = entry->Start;
		split.End = range.Start;
		entry->Start = range.End;
		ranges.Insert(entry - ranges.begin(), split);
	}
}

inline int LevelMesh::RemoveRange(TArray<MeshBufferRange>& ranges, int count)
{
	for (unsigned int i = ranges.Size(); i > 0; i--)
	{
		auto& item = ranges[i - 1];
		if (item.End - item.Start >= count)
		{
			int pos = item.Start;
			item.Start += count;
			if (item.Start == item.End)
			{
				ranges.Delete(i - 1);
			}
			return pos;
		}
	}
	I_FatalError("Could not find space in level mesh buffer");
}
