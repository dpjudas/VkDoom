
#include "hw_levelmesh.h"

LevelMeshSurface* LevelMesh::Trace(const FVector3& start, FVector3 direction, float maxDist)
{
	maxDist = std::max(maxDist - 10.0f, 0.0f);

	FVector3 origin = start;

	LevelMeshSurface* hitSurface = nullptr;

	while (true)
	{
		FVector3 end = origin + direction * maxDist;

		TraceHit hit0 = TriangleMeshShape::find_first_hit(StaticMesh->Collision.get(), origin, end);
		TraceHit hit1 = TriangleMeshShape::find_first_hit(DynamicMesh->Collision.get(), origin, end);

		LevelSubmesh* hitmesh = hit0.fraction < hit1.fraction ? StaticMesh.get() : DynamicMesh.get();
		TraceHit hit = hit0.fraction < hit1.fraction ? hit0 : hit1;

		if (hit.triangle < 0)
		{
			return nullptr;
		}

		hitSurface = hitmesh->GetSurface(hitmesh->MeshSurfaceIndexes[hit.triangle]);
		auto portal = hitSurface->portalIndex;

		if (!portal)
		{
			break;
		}

		auto& transformation = hitmesh->Portals[portal];

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

LevelMeshSurfaceStats LevelMesh::GatherSurfacePixelStats()
{
	LevelMeshSurfaceStats stats;
	StaticMesh->GatherSurfacePixelStats(stats);
	DynamicMesh->GatherSurfacePixelStats(stats);
	return stats;
}

/////////////////////////////////////////////////////////////////////////////

LevelSubmesh::LevelSubmesh()
{
	// Default portal
	LevelMeshPortal portal;
	Portals.Push(portal);

	// Default empty mesh (we can't make it completely empty since vulkan doesn't like that)
	float minval = -100001.0f;
	float maxval = -100000.0f;
	MeshVertices.Push({ minval, minval, minval });
	MeshVertices.Push({ maxval, minval, minval });
	MeshVertices.Push({ maxval, maxval, minval });
	MeshVertices.Push({ minval, minval, minval });
	MeshVertices.Push({ minval, maxval, minval });
	MeshVertices.Push({ maxval, maxval, minval });
	MeshVertices.Push({ minval, minval, maxval });
	MeshVertices.Push({ maxval, minval, maxval });
	MeshVertices.Push({ maxval, maxval, maxval });
	MeshVertices.Push({ minval, minval, maxval });
	MeshVertices.Push({ minval, maxval, maxval });
	MeshVertices.Push({ maxval, maxval, maxval });

	MeshVertexUVs.Resize(MeshVertices.Size());

	for (int i = 0; i < 3 * 4; i++)
		MeshElements.Push(i);

	UpdateCollision();
}

void LevelSubmesh::UpdateCollision()
{
	Collision = std::make_unique<TriangleMeshShape>(MeshVertices.Data(), MeshVertices.Size(), MeshElements.Data(), MeshElements.Size());
}

void LevelSubmesh::GatherSurfacePixelStats(LevelMeshSurfaceStats& stats)
{
	int count = GetSurfaceCount();
	for (int i = 0; i < count; ++i)
	{
		const auto* surface = GetSurface(i);
		auto area = surface->Area();

		stats.pixels.total += area;

		if (surface->needsUpdate)
		{
			stats.surfaces.dirty++;
			stats.pixels.dirty += area;
		}
		if (surface->bSky)
		{
			stats.surfaces.sky++;
			stats.pixels.sky += area;
		}
	}
	stats.surfaces.total += count;
}

void LevelSubmesh::BuildTileSurfaceLists()
{
	// Smoothing group surface is to be rendered with
	TArray<LevelMeshSmoothingGroup> SmoothingGroups;
	TArray<int> SmoothingGroupIndexes(GetSurfaceCount());

	for (int i = 0, count = GetSurfaceCount(); i < count; i++)
	{
		auto surface = GetSurface(i);

		// Is this surface in the same plane as an existing smoothing group?
		int smoothingGroupIndex = -1;

		for (size_t j = 0; j < SmoothingGroups.Size(); j++)
		{
			if (surface->sectorGroup == SmoothingGroups[j].sectorGroup)
			{
				float direction = SmoothingGroups[j].plane.XYZ() | surface->plane.XYZ();
				if (direction >= 0.9999f && direction <= 1.001f)
				{
					auto point = (surface->plane.XYZ() * surface->plane.W);
					auto planeDistance = (SmoothingGroups[j].plane.XYZ() | point) - SmoothingGroups[j].plane.W;

					float dist = std::abs(planeDistance);
					if (dist <= 0.01f)
					{
						smoothingGroupIndex = (int)j;
						break;
					}
				}
			}
		}

		// Surface is in a new plane. Create a smoothing group for it
		if (smoothingGroupIndex == -1)
		{
			smoothingGroupIndex = SmoothingGroups.Size();

			LevelMeshSmoothingGroup group;
			group.plane = surface->plane;
			group.sectorGroup = surface->sectorGroup;
			SmoothingGroups.Push(group);
		}

		SmoothingGroups[smoothingGroupIndex].surfaces.push_back(surface);
		SmoothingGroupIndexes.Push(smoothingGroupIndex);
	}

	for (int i = 0, count = GetSurfaceCount(); i < count; i++)
	{
		auto targetSurface = GetSurface(i);
		targetSurface->tileSurfaces.Clear();
		for (LevelMeshSurface* surface : SmoothingGroups[SmoothingGroupIndexes[i]].surfaces)
		{
			FVector2 minUV = ToUV(surface->bounds.min, targetSurface);
			FVector2 maxUV = ToUV(surface->bounds.max, targetSurface);
			if (surface != targetSurface && (maxUV.X < 0.0f || maxUV.Y < 0.0f || minUV.X > 1.0f || minUV.Y > 1.0f))
				continue; // Bounding box not visible

			targetSurface->tileSurfaces.Push(surface);
		}
	}
}

FVector2 LevelSubmesh::ToUV(const FVector3& vert, const LevelMeshSurface* targetSurface)
{
	FVector3 localPos = vert - targetSurface->translateWorldToLocal;
	float u = (1.0f + (localPos | targetSurface->projLocalToU)) / (targetSurface->AtlasTile.Width + 2);
	float v = (1.0f + (localPos | targetSurface->projLocalToV)) / (targetSurface->AtlasTile.Height + 2);
	return FVector2(u, v);
}
