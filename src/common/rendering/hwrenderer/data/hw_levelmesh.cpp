
#include "hw_levelmesh.h"
#include "halffloat.h"

LevelMesh::LevelMesh()
{
	LevelMeshLimits limits;
	limits.MaxVertices = 12;
	limits.MaxIndexes = 3 * 4;
	limits.MaxSurfaces = 1;
	limits.MaxUniforms = 1;
	Reset(limits);

	// Default portal
	LevelMeshPortal portal;
	Portals.Push(portal);

	AddEmptyMesh();
	UpdateCollision();

	Mesh.MaxNodes = (int)std::max(Collision->get_nodes().size() * 2, (size_t)10000);
}

void LevelMesh::Reset(const LevelMeshLimits& limits)
{
	Mesh.Vertices.Resize(limits.MaxVertices);
	Mesh.UniformIndexes.Resize(limits.MaxVertices);

	Mesh.Surfaces.Resize(limits.MaxSurfaces);
	Mesh.Uniforms.Resize(limits.MaxUniforms);
	Mesh.Materials.Resize(limits.MaxUniforms);

	Mesh.Lights.Resize(16);
	Mesh.LightIndexes.Resize(16);

	Mesh.Indexes.Resize(limits.MaxIndexes);
	Mesh.SurfaceIndexes.Resize(limits.MaxIndexes / 3 + 1);

	Mesh.DrawIndexes.Resize(limits.MaxIndexes);

	FreeLists.Vertex.Clear(); FreeLists.Vertex.Push({ 0, limits.MaxVertices });
	FreeLists.Index.Clear(); FreeLists.Index.Push({ 0, limits.MaxIndexes });
	FreeLists.Uniforms.Clear(); FreeLists.Uniforms.Push({ 0, limits.MaxUniforms });
	FreeLists.Surface.Clear(); FreeLists.Surface.Push({ 0, limits.MaxSurfaces });
	FreeLists.DrawIndex.Clear(); FreeLists.DrawIndex.Push({ 0, limits.MaxIndexes });
}

void LevelMesh::AddEmptyMesh()
{
	GeometryAllocInfo ginfo = AllocGeometry(12, 3 * 4);

	// Default empty mesh (we can't make it completely empty since vulkan doesn't like that)
	float minval = -100001.0f;
	float maxval = -100000.0f;
	ginfo.Vertices[0] = { minval, minval, minval };
	ginfo.Vertices[1] = { maxval, minval, minval };
	ginfo.Vertices[2] = { maxval, maxval, minval };
	ginfo.Vertices[3] = { minval, minval, minval };
	ginfo.Vertices[4] = { minval, maxval, minval };
	ginfo.Vertices[5] = { maxval, maxval, minval };
	ginfo.Vertices[6] = { minval, minval, maxval };
	ginfo.Vertices[7] = { maxval, minval, maxval };
	ginfo.Vertices[8] = { maxval, maxval, maxval };
	ginfo.Vertices[9] = { minval, minval, maxval };
	ginfo.Vertices[10] = { minval, maxval, maxval };
	ginfo.Vertices[11] = { maxval, maxval, maxval };

	for (int i = 0; i < 3 * 4; i++)
		ginfo.Indexes[i] = i;

	Mesh.IndexCount = ginfo.IndexCount;

	UploadPortals();
}

LevelMeshSurface* LevelMesh::Trace(const FVector3& start, FVector3 direction, float maxDist)
{
	maxDist = std::max(maxDist - 10.0f, 0.0f);

	FVector3 origin = start;

	LevelMeshSurface* hitSurface = nullptr;

	while (true)
	{
		FVector3 end = origin + direction * maxDist;

		TraceHit hit = TriangleMeshShape::find_first_hit(Collision.get(), origin, end);

		if (hit.triangle < 0)
		{
			return nullptr;
		}

		hitSurface = &Mesh.Surfaces[Mesh.SurfaceIndexes[hit.triangle]];

		int portal = hitSurface->PortalIndex;
		if (!portal)
		{
			break;
		}

		auto& transformation = Portals[portal];

		auto travelDist = hit.fraction * maxDist + 2.0f;
		if (travelDist >= maxDist)
		{
			break;
		}

		origin = transformation.TransformPosition(origin + direction * travelDist);
		direction = transformation.TransformRotation(direction);
		maxDist -= travelDist;
	}

	return hitSurface; // I hit something
}

LevelMeshTileStats LevelMesh::GatherTilePixelStats()
{
	LevelMeshTileStats stats;
	for (const LightmapTile& tile : LightmapTiles)
	{
		auto area = tile.AtlasLocation.Area();

		stats.pixels.total += area;

		if (tile.NeedsUpdate)
		{
			stats.tiles.dirty++;
			stats.pixels.dirty += area;
		}
	}
	stats.tiles.total += LightmapTiles.Size();
	return stats;
}

void LevelMesh::UpdateCollision()
{
	Collision = std::make_unique<TriangleMeshShape>(Mesh.Vertices.Data(), Mesh.Vertices.Size(), Mesh.Indexes.Data(), Mesh.IndexCount);
	UploadCollision();
}

struct LevelMeshPlaneGroup
{
	FVector4 plane = FVector4(0, 0, 1, 0);
	int sectorGroup = 0;
	std::vector<LevelMeshSurface*> surfaces;
};

void LevelMesh::SetupTileTransforms()
{
	for (auto& tile : LightmapTiles)
	{
		tile.SetupTileTransform(LMTextureSize);
	}
}

void LevelMesh::PackLightmapAtlas(int lightmapStartIndex)
{
	LMAtlasPacked = true;

	std::vector<LightmapTile*> sortedTiles;
	sortedTiles.reserve(LightmapTiles.Size());

	for (auto& tile : LightmapTiles)
	{
		sortedTiles.push_back(&tile);
	}

	std::sort(sortedTiles.begin(), sortedTiles.end(), [](LightmapTile* a, LightmapTile* b) { return a->AtlasLocation.Height != b->AtlasLocation.Height ? a->AtlasLocation.Height > b->AtlasLocation.Height : a->AtlasLocation.Width > b->AtlasLocation.Width; });

	// We do not need to add spacing here as this is already built into the tile size itself.
	RectPacker packer(LMTextureSize, LMTextureSize, RectPacker::Spacing(0), RectPacker::Padding(0));

	for (LightmapTile* tile : sortedTiles)
	{
		auto result = packer.insert(tile->AtlasLocation.Width, tile->AtlasLocation.Height);
		tile->AtlasLocation.X = result.pos.x;
		tile->AtlasLocation.Y = result.pos.y;
		tile->AtlasLocation.ArrayIndex = lightmapStartIndex + (int)result.pageIndex;
	}

	LMTextureCount = (int)packer.getNumPages();

	// Calculate final texture coordinates
	for (int i = 0, count = Mesh.Surfaces.Size(); i < count; i++)
	{
		auto surface = &Mesh.Surfaces[i];
		if (surface->LightmapTileIndex >= 0)
		{
			const LightmapTile& tile = LightmapTiles[surface->LightmapTileIndex];
			for (int i = 0; i < surface->MeshLocation.NumVerts; i++)
			{
				auto& vertex = Mesh.Vertices[surface->MeshLocation.StartVertIndex + i];
				FVector2 uv = tile.ToUV(vertex.fPos(), (float)LMTextureSize);
				vertex.lu = uv.X;
				vertex.lv = uv.Y;
				vertex.lindex = (float)tile.AtlasLocation.ArrayIndex;
			}
		}
	}

#if 0 // Debug atlas tile locations:
	float colors[30] =
	{
		1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		1.0f, 1.0f, 0.0f,
		0.0f, 1.0f, 1.0f,
		1.0f, 0.0f, 1.0f,
		0.5f, 0.0f, 0.0f,
		0.0f, 0.5f, 0.0f,
		0.5f, 0.5f, 0.0f,
		0.0f, 0.5f, 0.5f,
		0.5f, 0.0f, 0.5f
	};
	LMTextureData.Resize(LMTextureSize * LMTextureSize * LMTextureCount * 3);
	uint16_t* pixels = LMTextureData.Data();
	for (LightmapTile& tile : LightmapTiles)
	{
		tile.NeedsUpdate = false;

		int index = tile.Binding.TypeIndex;
		float* color = colors + (index % 10) * 3;

		int x = tile.AtlasLocation.X;
		int y = tile.AtlasLocation.Y;
		int w = tile.AtlasLocation.Width;
		int h = tile.AtlasLocation.Height;
		for (int yy = y; yy < y + h; yy++)
		{
			uint16_t* line = pixels + tile.AtlasLocation.ArrayIndex * LMTextureSize * LMTextureSize + yy * LMTextureSize * 3;
			for (int xx = x; xx < x + w; xx++)
			{
				float gray = (yy - y) / (float)h;
				line[xx * 3] = floatToHalf(color[0] * gray);
				line[xx * 3 + 1] = floatToHalf(color[1] * gray);
				line[xx * 3 + 2] = floatToHalf(color[2] * gray);
			}
		}
	}
	for (int i = 0, count = GetSurfaceCount(); i < count; i++)
	{
		auto surface = GetSurface(i);
		surface->AlwaysUpdate = false;
	}
#endif
}
