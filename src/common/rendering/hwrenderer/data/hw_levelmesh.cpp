
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
	CreateCollision();
}

void LevelMesh::CreateCollision()
{
	Collision = std::make_unique<CPUAccelStruct>(this);
}

void LevelMesh::Reset(const LevelMeshLimits& limits)
{
	Mesh.Vertices.Resize(limits.MaxVertices);
	Mesh.UniformIndexes.Resize(limits.MaxVertices);

	Mesh.Surfaces.Resize(limits.MaxSurfaces);
	Mesh.Uniforms.Resize(limits.MaxUniforms);
	Mesh.LightUniforms.Resize(limits.MaxUniforms);
	Mesh.Materials.Resize(limits.MaxUniforms);

	int maxLights = 20'000;
	int maxDynlights = 50'000;

	Mesh.Lights.Resize(maxLights);
	Mesh.LightIndexes.Resize(limits.MaxSurfaces * 10);
	Mesh.DynLights.Resize(maxDynlights * 4);

	Mesh.Indexes.Resize(limits.MaxIndexes);
	Mesh.SurfaceIndexes.Resize(limits.MaxIndexes / 3 + 1);

	Mesh.DrawIndexes.Resize(limits.MaxIndexes);

	FreeLists.Vertex.Reset(limits.MaxVertices);
	FreeLists.Index.Reset(limits.MaxIndexes);
	FreeLists.Uniforms.Reset(limits.MaxUniforms);
	FreeLists.Surface.Reset(limits.MaxSurfaces);
	FreeLists.DrawIndex.Reset(limits.MaxIndexes);
	FreeLists.LightIndex.Reset(limits.MaxSurfaces * 10);
	FreeLists.Light.Reset(maxLights);
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

		TraceHit hit = Collision->FindFirstHit(origin, end);

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
	for (const LightmapTile& tile : Lightmap.Tiles)
	{
		auto area = tile.AtlasLocation.Area();

		stats.pixels.total += area;

		if (tile.NeedsUpdate)
		{
			stats.tiles.dirty++;
			stats.pixels.dirty += area;
		}
	}
	stats.tiles.total += Lightmap.Tiles.Size();
	return stats;
}

void LevelMesh::PackStaticLightmapAtlas()
{
	Lightmap.StaticAtlasPacked = true;
	Lightmap.DynamicTilesStart = Lightmap.Tiles.Size();

	for (auto& tile : Lightmap.Tiles)
	{
		if (tile.NeedsUpdate) // false for tiles loaded from lump
			tile.SetupTileTransform(Lightmap.TextureSize);
	}

	std::vector<LightmapTile*> sortedTiles;
	sortedTiles.reserve(Lightmap.Tiles.Size());
	for (auto& tile : Lightmap.Tiles)
		sortedTiles.push_back(&tile);

	std::sort(sortedTiles.begin(), sortedTiles.end(), [](LightmapTile* a, LightmapTile* b) { return a->AtlasLocation.Height != b->AtlasLocation.Height ? a->AtlasLocation.Height > b->AtlasLocation.Height : a->AtlasLocation.Width > b->AtlasLocation.Width; });

	// We do not need to add spacing here as this is already built into the tile size itself.
	RectPacker packer(Lightmap.TextureSize, Lightmap.TextureSize, RectPacker::Spacing(0), RectPacker::Padding(0));

	for (LightmapTile* tile : sortedTiles)
	{
		auto result = packer.insert(tile->AtlasLocation.Width, tile->AtlasLocation.Height);
		tile->AtlasLocation.X = result.pos.x;
		tile->AtlasLocation.Y = result.pos.y;
		tile->AtlasLocation.ArrayIndex = (int)result.pageIndex;
	}

	Lightmap.TextureCount = (int)packer.getNumPages();

	// Calculate final texture coordinates
	for (int i = 0, count = Mesh.Surfaces.Size(); i < count; i++)
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
}

void LevelMesh::ClearDynamicLightmapAtlas()
{
	for (int surfIndex : Lightmap.DynamicSurfaces)
	{
		Mesh.Surfaces[surfIndex].LightmapTileIndex = -1;
	}
	Lightmap.DynamicSurfaces.Clear();
	Lightmap.Tiles.Resize(Lightmap.DynamicTilesStart);
}

void LevelMesh::PackDynamicLightmapAtlas()
{
	std::vector<LightmapTile*> sortedTiles;
	sortedTiles.reserve(Lightmap.Tiles.Size() - Lightmap.DynamicTilesStart);

	for (unsigned int i = Lightmap.DynamicTilesStart; i < Lightmap.Tiles.Size(); i++)
	{
		Lightmap.Tiles[i].SetupTileTransform(Lightmap.TextureSize);
		sortedTiles.push_back(&Lightmap.Tiles[i]);
	}

	std::sort(sortedTiles.begin(), sortedTiles.end(), [](LightmapTile* a, LightmapTile* b) { return a->AtlasLocation.Height != b->AtlasLocation.Height ? a->AtlasLocation.Height > b->AtlasLocation.Height : a->AtlasLocation.Width > b->AtlasLocation.Width; });

	// We do not need to add spacing here as this is already built into the tile size itself.
	RectPacker packer(Lightmap.TextureSize, Lightmap.TextureSize, RectPacker::Spacing(0), RectPacker::Padding(0));

	for (LightmapTile* tile : sortedTiles)
	{
		auto result = packer.insert(tile->AtlasLocation.Width, tile->AtlasLocation.Height);
		tile->AtlasLocation.X = result.pos.x;
		tile->AtlasLocation.Y = result.pos.y;
		tile->AtlasLocation.ArrayIndex = Lightmap.TextureCount; // (int)result.pageIndex;
	}

	for (int surfIndex : Lightmap.DynamicSurfaces)
	{
		auto surface = &Mesh.Surfaces[surfIndex];
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
}

/////////////////////////////////////////////////////////////////////////////

void MeshBufferAllocator::Reset(int size)
{
	TotalSize = size;
	Unused.Clear();
	Unused.Push({ 0, size });
}

int MeshBufferAllocator::GetTotalSize() const
{
	return TotalSize;
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

	I_FatalError("Could not find space in level mesh buffer");
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

/////////////////////////////////////////////////////////////////////////////

void LevelMeshDrawList::Add(int position, int count)
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

void LevelMeshDrawList::Remove(int position, int count)
{
	if (count <= 0)
		return;

	MeshBufferRange range = { position, position + count };

	auto entry = std::lower_bound(Ranges.begin(), Ranges.end(), range, [](const auto& a, const auto& b) { return a.End < b.End; });
	if (entry->Start == range.Start && entry->End == range.End)
	{
		Ranges.Delete(entry - Ranges.begin());
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
		Ranges.Insert(entry - Ranges.begin(), split);
	}
}
