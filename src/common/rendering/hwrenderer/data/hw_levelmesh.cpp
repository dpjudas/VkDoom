
#include "hw_levelmesh.h"
#include "halffloat.h"
#include "hw_dynlightdata.h"

LevelMesh::LevelMesh()
{
	Lightmap.AtlasPacker = std::make_unique<RectPacker>(Lightmap.TextureSize, Lightmap.TextureSize, 0);

	Reset();

	// Default portal
	LevelMeshPortal portal;
	Portals.Push(portal);

	AddEmptyMesh();
	CreateCollision();
}

void LevelMesh::CreateCollision()
{
	Collision = std::make_unique<CPUAccelStruct>(this);
}

void LevelMesh::Reset()
{
	Mesh.Vertices.Clear();
	Mesh.UniformIndexes.Clear();

	Mesh.Surfaces.Clear();
	Mesh.Uniforms.Clear();
	Mesh.LightUniforms.Clear();
	Mesh.Materials.Clear();

	int maxLights = 20'000;

	Mesh.Lights.Resize(maxLights);
	Mesh.LightIndexes.Clear();
	Mesh.DynLights.Resize((sizeof(int) * 4) + MAX_LIGHT_DATA * sizeof(FDynLightInfo));

	Mesh.Indexes.Clear();
	Mesh.SurfaceIndexes.Clear();

	FreeLists.Vertex.Reset(Mesh.Vertices.Size());
	FreeLists.Index.Reset(Mesh.Indexes.Size());
	FreeLists.Uniforms.Reset(Mesh.Uniforms.Size());
	FreeLists.Surface.Reset(Mesh.Surfaces.Size());
	FreeLists.LightIndex.Reset(Mesh.LightIndexes.Size());
	FreeLists.Light.Reset(Mesh.Lights.Size());
}

void LevelMesh::AddEmptyMesh()
{
	GeometryAllocInfo ginfo = AllocGeometry(12, 3 * 4);

	// Default empty mesh (we can't make it completely empty since vulkan doesn't like that)
	float minval = -100001.0f;
	float maxval = -100000.0f;
	auto vertices = GetVertices(ginfo);
	vertices[0] = { minval, minval, minval };
	vertices[1] = { maxval, minval, minval };
	vertices[2] = { maxval, maxval, minval };
	vertices[3] = { minval, minval, minval };
	vertices[4] = { minval, maxval, minval };
	vertices[5] = { maxval, maxval, minval };
	vertices[6] = { minval, minval, maxval };
	vertices[7] = { maxval, minval, maxval };
	vertices[8] = { maxval, maxval, maxval };
	vertices[9] = { minval, minval, maxval };
	vertices[10] = { minval, maxval, maxval };
	vertices[11] = { maxval, maxval, maxval };

	auto indexes = GetIndexes(ginfo);
	for (int i = 0; i < 3 * 4; i++)
		indexes[i] = i;

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

		TraceHit hit = Collision->FindFirstHit(origin, end);

		if (hit.triangle < 0)
		{
			return nullptr;
		}

		hitSurface = &Mesh.Surfaces[Mesh.SurfaceIndexes[hit.triangle]];

		int portal = hitSurface->PortalIndex;
		if (!portal && hitSurface->Alpha >= 1.0)
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
	for (const LightmapTile& tile : Lightmap.Tiles)
	{
		auto area = tile.AtlasLocation.Area();

		stats.pixels.total += area;

		if (tile.NeedsInitialBake)
		{
			stats.tiles.dirty++;
			stats.pixels.dirty += area;
		}
	}
	stats.tiles.total += Lightmap.Tiles.Size();
	return stats;
}

void LevelMesh::PackLightmapAtlas()
{
	// Get tiles that needs to be put into the atlas:

	std::vector<LightmapTile*> sortedTiles;
	sortedTiles.reserve(Lightmap.AddedTiles.Size());

	for (int index : Lightmap.AddedTiles)
	{
		LightmapTile& tile = Lightmap.Tiles[index];

		tile.AddedThisFrame = false;

		if (tile.NeedsInitialBake || tile.GeometryUpdate) // NeedsInitialBake is false for tiles loaded from lump
			tile.SetupTileTransform(Lightmap.TextureSize);

		sortedTiles.push_back(&tile);
	}

	// Sort them by size
	std::sort(sortedTiles.begin(), sortedTiles.end(), [](LightmapTile* a, LightmapTile* b) { return a->AtlasLocation.Height != b->AtlasLocation.Height ? a->AtlasLocation.Height > b->AtlasLocation.Height : a->AtlasLocation.Width > b->AtlasLocation.Width; });

	// Find places in the atlas for the tiles
	for (LightmapTile* tile : sortedTiles)
	{
		auto result = Lightmap.AtlasPacker->Alloc(tile->AtlasLocation.Width, tile->AtlasLocation.Height);
		tile->AtlasLocation.X = result->X;
		tile->AtlasLocation.Y = result->Y;
		tile->AtlasLocation.ArrayIndex = (int)result->PageIndex;
		tile->AtlasLocation.Item = result;
	}

	Lightmap.TextureCount = (int)Lightmap.AtlasPacker->GetNumPages();

	// Calculate final texture coordinates
	for (int i : Lightmap.AddedSurfaces)
	{
		auto surface = &Mesh.Surfaces[i];
		if (surface->LightmapTileIndex >= 0)
		{
			const LightmapTile& tile = Lightmap.Tiles[surface->LightmapTileIndex];
			for (int i = 0; i < surface->MeshLocation.NumVerts; i++)
			{
				auto& vertex = Mesh.Vertices[surface->MeshLocation.StartVertIndex + i];
				FVector2 uv = tile.ToUV(vertex.fPos(), (float)Lightmap.TextureSize);
				vertex.lu = uv.X;
				vertex.lv = uv.Y;
				vertex.lindex = (float)tile.AtlasLocation.ArrayIndex;
			}
		}
	}

	Lightmap.AddedTiles.Clear();
	Lightmap.AddedSurfaces.Clear();
}

/////////////////////////////////////////////////////////////////////////////

void MeshBufferAllocator::Reset(int size)
{
	TotalSize = size;
	Unused.Clear();
	if (size > 0)
		Unused.Push({ 0, size });
}

void MeshBufferAllocator::Grow(int amount)
{
	amount = (amount + TotalSize) * 2;

	if (Unused.Size() != 0 && Unused.back().End == TotalSize)
	{
		TotalSize += amount;
		Unused.back().End = TotalSize;
	}
	else
	{
		Unused.Push({ TotalSize, TotalSize + amount });
		TotalSize += amount;
	}
}

int MeshBufferAllocator::GetUsedSize() const
{
	int used = TotalSize;
	for (auto& range : Unused)
	{
		int count = range.End - range.Start;
		used -= count;
	}
	return used;
}

int MeshBufferAllocator::Alloc(int count)
{
	for (unsigned int i = 0, size = Unused.Size(); i < size; i++)
	{
		auto& item = Unused[i];
		if (item.End - item.Start >= count)
		{
			int pos = item.Start;
			item.Start += count;
			if (item.Start == item.End)
			{
				Unused.Delete(i);
			}
			return pos;
		}
	}

	return -1;
}

void MeshBufferAllocator::Free(int position, int count)
{
	if (count <= 0)
		return;

	MeshBufferRange range = { position, position + count };

	// First element?
	if (Unused.Size() == 0)
	{
		Unused.push_back(range);
		return;
	}

	// Find start position in ranges
	auto right = std::lower_bound(Unused.begin(), Unused.end(), range, [](const auto& a, const auto& b) { return a.Start < b.Start; });
	bool leftExists = right != Unused.begin();
	bool rightExists = right != Unused.end();
	auto left = right;
	if (leftExists)
		--left;

	// Is this a gap between two ranges?
	if ((!leftExists || left->End < range.Start) && (!rightExists || right->Start > range.End))
	{
		Unused.Insert(right - Unused.begin(), range);
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
		if (right == Unused.end() || right->Start > range.End)
			break;
		left->End = std::max(right->End, range.End);
	}

	// Remove ranges now covered by the extended range
	//Unused.erase(++left, right);
	++left;
	auto leftPos = left - Unused.begin();
	auto rightPos = right - Unused.begin();
	Unused.Delete(leftPos, rightPos - leftPos);
}

/////////////////////////////////////////////////////////////////////////////

void MeshBufferUploads::Clear()
{
	Ranges.Clear();
}

void MeshBufferUploads::Add(int position, int count)
{
	if (count <= 0)
		return;

	MeshBufferRange range = { position, position + count };

	// First element?
	if (Ranges.Size() == 0)
	{
		Ranges.push_back(range);
		return;
	}

	// Find start position in ranges
	auto right = std::lower_bound(Ranges.begin(), Ranges.end(), range, [](const auto& a, const auto& b) { return a.Start < b.Start; });
	bool leftExists = right != Ranges.begin();
	bool rightExists = right != Ranges.end();
	auto left = right;
	if (leftExists)
		--left;

	// Is this a gap between two ranges?
	if ((!leftExists || left->End < range.Start) && (!rightExists || right->Start > range.End))
	{
		Ranges.Insert(right - Ranges.begin(), range);
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
		if (right == Ranges.end() || right->Start > range.End)
			break;
		left->End = std::max(right->End, range.End);
	}

	// Remove ranges now covered by the extended range
	//ranges.erase(++left, right);
	++left;
	auto leftPos = left - Ranges.begin();
	auto rightPos = right - Ranges.begin();
	Ranges.Delete(leftPos, rightPos - leftPos);
}
