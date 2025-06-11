
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
#include "hw_rectpacker.h"
#include "engineerrors.h"
#include <memory>
#include <unordered_map>

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
	SurfaceLightUniforms* LightUniforms = nullptr;
	FMaterialState* Materials = nullptr;
	int Start = 0;
	int Count = 0;
};

struct SurfaceAllocInfo
{
	LevelMeshSurface* Surface = nullptr;
	int Index = 0;
	int Count = 0;
};

struct LightAllocInfo
{
	LevelMeshLight* Light = nullptr;
	int Index = 0;
};

struct LightListAllocInfo
{
	int32_t* List = nullptr;
	int Start = 0;
	int Count = 0;
};

struct MeshBufferRange
{
	int Start = 0;
	int End = 0;

	int Count() const { return End - Start; }
};

class MeshBufferAllocator
{
public:
	void Reset(int size);
	void Grow(int amount);

	int GetTotalSize() const { return TotalSize; }
	int GetUsedSize() const;

	int Alloc(int count);
	void Free(int position, int count);

private:
	int TotalSize = 0;
	TArray<MeshBufferRange> Unused;
};

class MeshBufferUploads
{
public:
	void Clear();
	void Add(int position, int count);
	const TArray<MeshBufferRange>& GetRanges() const { return Ranges; }

private:
	TArray<MeshBufferRange> Ranges;
};

class LevelMesh
{
public:
	LevelMesh();
	virtual ~LevelMesh() = default;


	virtual void FullRefresh() {};

	LevelMeshSurface* Trace(const FVector3& start, FVector3 direction, float maxDist);

	LevelMeshTileStats GatherTilePixelStats();

	// Map defaults
	FVector3 SunDirection = FVector3(0.0f, 0.0f, -1.0f);
	FVector3 SunColor = FVector3(0.0f, 0.0f, 0.0f);
	float SunIntensity = 1.0f;
	bool AmbientOcclusion = true;
	bool LightBounce = true;
	bool LevelWideLMDynamic = false; // Whole map has dynamic lightmaps enabled via ZDRayInfo (no need to set it per sector)

	TArray<LevelMeshPortal> Portals;

	GeometryAllocInfo AllocGeometry(int vertexCount, int indexCount);
	UniformsAllocInfo AllocUniforms(int count);
	SurfaceAllocInfo AllocSurface(int count = 1);
	LightAllocInfo AllocLight();
	LightListAllocInfo AllocLightList(int count);
	int AllocTile(const LightmapTile& tile);

	void FreeGeometry(int vertexStart, int vertexCount, int indexStart, int indexCount);
	void FreeUniforms(int start, int count);
	void FreeSurface(unsigned int surfaceIndex, int count = 1);
	void FreeLightList(int start, int count);
	void FreeTile(int index);

	// Sets the initial sizes of the GPU buffers and empties the mesh
	virtual void Reset();

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
		TArray<SurfaceLightUniforms> LightUniforms;
		TArray<FMaterialState> Materials;
		TArray<int32_t> LightIndexes;

		// Lights
		TArray<LevelMeshLight> Lights;
		TArray<uint8_t> DynLights;

		// Index data
		TArray<uint32_t> Indexes;
		TArray<int> SurfaceIndexes;
		int IndexCount = 0; // Index range filled with data

		// Acceleration structure nodes for when the GPU doesn't support rayquery
		TArray<CollisionNode> Nodes;
		int RootNode = 0;
	} Mesh;

	// Ranges in mesh that have changed since last upload
	struct
	{
		MeshBufferUploads Vertex;
		MeshBufferUploads Index;
		MeshBufferUploads Node;
		MeshBufferUploads SurfaceIndex;
		MeshBufferUploads Surface;
		MeshBufferUploads UniformIndexes;
		MeshBufferUploads Uniforms;
		MeshBufferUploads LightUniforms;
		MeshBufferUploads Portals;
		MeshBufferUploads Light;
		MeshBufferUploads LightIndex;
		MeshBufferUploads DynLight;
	} UploadRanges;

	// Ranges in mesh currently not in use
	struct
	{
		MeshBufferAllocator Vertex;
		MeshBufferAllocator Index;
		MeshBufferAllocator Uniforms;
		MeshBufferAllocator Surface;
		MeshBufferAllocator Light;
		MeshBufferAllocator LightIndex;
	} FreeLists;

	// Data structure for doing mesh traces on the CPU
	std::unique_ptr<CPUAccelStruct> Collision;

	// Lightmap tiles and their locations in the texture atlas
	struct
	{
		int TextureCount = 10; // To do: remove all the limits and instead resize GPU resources if they are exhausted. Too difficult to calculate it all up front.
		int TextureSize = 1024;
		TArray<uint16_t> TextureData;
		uint16_t SampleDistance = 8;
		TArray<LightmapTile> Tiles;
		int UsedTiles = 0;
		TArray<int> FreeTiles;
		TArray<int> AddedTiles;
		TArray<int> AddedSurfaces;
		std::unique_ptr<RectPacker> AtlasPacker;
	} Lightmap;

	uint32_t AtlasPixelCount() const { return uint32_t(Lightmap.TextureCount * Lightmap.TextureSize * Lightmap.TextureSize); }
	void PackLightmapAtlas();

	void AddEmptyMesh();
	
	void UploadPortals();
	void CreateCollision();
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
	info.VertexStart = FreeLists.Vertex.Alloc(vertexCount);
	if (info.VertexStart == -1)
	{
		FreeLists.Vertex.Grow(vertexCount);
		Mesh.Vertices.Resize(FreeLists.Vertex.GetTotalSize());
		Mesh.UniformIndexes.Resize(FreeLists.Vertex.GetTotalSize());
		info.VertexStart = FreeLists.Vertex.Alloc(vertexCount);
		if (info.VertexStart == -1)
			I_FatalError("Could not find space in level mesh vertex buffer");
	}
	info.VertexCount = vertexCount;
	info.IndexStart = FreeLists.Index.Alloc(indexCount);
	if (info.IndexStart == -1)
	{
		FreeLists.Index.Grow(indexCount);
		Mesh.Indexes.Resize(FreeLists.Index.GetTotalSize());
		Mesh.SurfaceIndexes.Resize(FreeLists.Index.GetTotalSize() / 3 + 1);
		info.IndexStart = FreeLists.Index.Alloc(indexCount);
		if (info.IndexStart == -1)
			I_FatalError("Could not find space in level mesh index buffer");
	}
	info.IndexCount = indexCount;
	info.Vertices = &Mesh.Vertices[info.VertexStart];
	info.UniformIndexes = &Mesh.UniformIndexes[info.VertexStart];
	info.Indexes = &Mesh.Indexes[info.IndexStart];

	UploadRanges.Vertex.Add(info.VertexStart, info.VertexCount);
	UploadRanges.UniformIndexes.Add(info.VertexStart, info.VertexCount);
	UploadRanges.Index.Add(info.IndexStart, info.IndexCount);
	UploadRanges.SurfaceIndex.Add(info.IndexStart / 3, info.IndexCount / 3);

	Mesh.IndexCount = std::max(Mesh.IndexCount, info.IndexStart + info.IndexCount);

	return info;
}

inline UniformsAllocInfo LevelMesh::AllocUniforms(int count)
{
	UniformsAllocInfo info;
	info.Start = FreeLists.Uniforms.Alloc(count);
	if (info.Start == -1)
	{
		FreeLists.Uniforms.Grow(count);
		Mesh.Uniforms.Resize(FreeLists.Uniforms.GetTotalSize());
		Mesh.LightUniforms.Resize(FreeLists.Uniforms.GetTotalSize());
		Mesh.Materials.Resize(FreeLists.Uniforms.GetTotalSize());
		info.Start = FreeLists.Uniforms.Alloc(count);
		if (info.Start == -1)
			I_FatalError("Could not find space in level mesh uniform buffer");
	}
	info.Count = count;
	info.Uniforms = &Mesh.Uniforms[info.Start];
	info.LightUniforms = &Mesh.LightUniforms[info.Start];
	info.Materials = &Mesh.Materials[info.Start];

	UploadRanges.Uniforms.Add(info.Start, info.Count);
	UploadRanges.LightUniforms.Add(info.Start, info.Count);

	return info;
}

inline LightListAllocInfo LevelMesh::AllocLightList(int count)
{
	LightListAllocInfo info;
	info.Start = FreeLists.LightIndex.Alloc(count);
	if (info.Start == -1)
	{
		FreeLists.LightIndex.Grow(count);
		Mesh.LightIndexes.Resize(FreeLists.LightIndex.GetTotalSize());
		info.Start = FreeLists.LightIndex.Alloc(count);
		if (info.Start == -1)
			I_FatalError("Could not find space in level mesh light index buffer");
	}
	info.Count = count;
	info.List = &Mesh.LightIndexes[info.Start];
	UploadRanges.LightIndex.Add(info.Start, info.Count);
	return info;
}

inline LightAllocInfo LevelMesh::AllocLight()
{
	LightAllocInfo info;
	info.Index = FreeLists.Light.Alloc(1);
	if (info.Index == -1)
	{
		FreeLists.Light.Grow(1);
		Mesh.Lights.Resize(FreeLists.Light.GetTotalSize());
		info.Index = FreeLists.Light.Alloc(1);
		if (info.Index == -1)
			I_FatalError("Could not find space in level mesh light buffer");
	}
	info.Light = &Mesh.Lights[info.Index];
	UploadRanges.Light.Add(info.Index, 1);
	return info;
}

inline SurfaceAllocInfo LevelMesh::AllocSurface(int count)
{
	SurfaceAllocInfo info;
	info.Index = FreeLists.Surface.Alloc(count);
	if (info.Index == -1)
	{
		FreeLists.Surface.Grow(count);
		Mesh.Surfaces.Resize(FreeLists.Surface.GetTotalSize());
		info.Index = FreeLists.Surface.Alloc(count);
		if (info.Index == -1)
			I_FatalError("Could not find space in level mesh surface buffer");
	}
	info.Surface = &Mesh.Surfaces[info.Index];
	info.Count = count;
	UploadRanges.Surface.Add(info.Index, info.Count);
	return info;
}

inline int LevelMesh::AllocTile(const LightmapTile& tile)
{
	Lightmap.UsedTiles++;
	if (Lightmap.FreeTiles.Size() != 0)
	{
		int index = Lightmap.FreeTiles.Last();
		Lightmap.FreeTiles.Pop();
		Lightmap.Tiles[index] = tile;
		return index;
	}
	int index = Lightmap.Tiles.Size();
	Lightmap.Tiles.Push(tile);
	return index;
}

inline void LevelMesh::FreeGeometry(int vertexStart, int vertexCount, int indexStart, int indexCount)
{
	// Convert triangles to degenerates
	for (int i = 0; i < indexCount; i++)
		Mesh.Indexes[indexStart + i] = 0;
	UploadRanges.Index.Add(indexStart, indexCount);

	FreeLists.Vertex.Free(vertexStart, vertexCount);
	FreeLists.Index.Free(indexStart, indexCount);
}

inline void LevelMesh::FreeUniforms(int start, int count)
{
	FreeLists.Uniforms.Free(start, count);
}

inline void LevelMesh::FreeLightList(int start, int count)
{
	FreeLists.LightIndex.Free(start, count);
}

inline void LevelMesh::FreeSurface(unsigned int surfaceIndex, int count)
{
	FreeLists.Surface.Free(surfaceIndex, count);
}

inline void LevelMesh::FreeTile(int index)
{
	Lightmap.UsedTiles--;
	Lightmap.FreeTiles.Push(index);
}

inline void LevelMesh::UploadPortals()
{
	UploadRanges.Portals.Clear();
	UploadRanges.Portals.Add(0, (int)Portals.Size());
}
