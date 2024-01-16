
#include "templates.h"
#include "doom_levelmesh.h"
#include "doom_levelsubmesh.h"
#include "g_levellocals.h"
#include "texturemanager.h"
#include "playsim/p_lnspec.h"
#include "c_dispatch.h"
#include "g_levellocals.h"
#include "a_dynlight.h"
#include "hw_renderstate.h"

static bool RequireLevelMesh()
{
	if (level.levelMesh)
		return true;

	Printf("No level mesh. Perhaps your level has no lightmap loaded?\n");
	return false;
}

static bool RequireLightmap()
{
	if (!RequireLevelMesh())
		return false;

	if (level.lightmaps)
		return true;

	Printf("Lightmap is not enabled in this level.\n");
	return false;
}

ADD_STAT(lightmap)
{
	FString out;
	DoomLevelMesh* levelMesh = level.levelMesh;

	if (!levelMesh || !level.lightmaps)
	{
		out.Format("No lightmap");
		return out;
	}

	uint32_t atlasPixelCount = levelMesh->StaticMesh->AtlasPixelCount();
	auto stats = levelMesh->GatherTilePixelStats();

	out.Format("Surfaces: %u (awaiting updates: %u)\nSurface pixel area to update: %u\nSurface pixel area: %u\nAtlas pixel area:   %u\nAtlas efficiency: %.4f%%",
		stats.tiles.total, stats.tiles.dirty,
		stats.pixels.dirty,
		stats.pixels.total,
		atlasPixelCount,
		float(stats.pixels.total) / float(atlasPixelCount) * 100.0f );

	return out;
}

CCMD(dumplevelmesh)
{
	if (!RequireLevelMesh()) return;
	level.levelMesh->DumpMesh(FString("levelmesh.obj"), FString("levelmesh.mtl"));
	Printf("Level mesh exported.\n");
}

CCMD(invalidatelightmap)
{
	if (!RequireLightmap()) return;

	int count = 0;
	for (auto& tile : level.levelMesh->StaticMesh->LightmapTiles)
	{
		if (!tile.NeedsUpdate)
			++count;
		tile.NeedsUpdate = true;
	}
	Printf("Marked %d out of %d tiles for update.\n", count, level.levelMesh->StaticMesh->LightmapTiles.Size());
}

void PrintSurfaceInfo(const DoomLevelMeshSurface* surface)
{
	if (!RequireLevelMesh()) return;

	auto gameTexture = surface->Texture;

	Printf("Surface %d (%p)\n    Type: %d, TypeIndex: %d, ControlSector: %d\n", surface->Submesh->GetSurfaceIndex(surface), surface, surface->Type, surface->TypeIndex, surface->ControlSector ? surface->ControlSector->Index() : -1);
	if (surface->LightmapTileIndex >= 0)
	{
		LightmapTile* tile = &surface->Submesh->LightmapTiles[surface->LightmapTileIndex];
		Printf("    Atlas page: %d, x:%d, y:%d\n", tile->AtlasLocation.ArrayIndex, tile->AtlasLocation.X, tile->AtlasLocation.Y);
		Printf("    Pixels: %dx%d (area: %d)\n", tile->AtlasLocation.Width, tile->AtlasLocation.Height, tile->AtlasLocation.Area());
		Printf("    Sample dimension: %d\n", tile->SampleDimension);
		Printf("    Needs update?: %d\n", tile->NeedsUpdate);
	}
	Printf("    Always update?: %d\n", surface->AlwaysUpdate);
	Printf("    Sector group: %d\n", surface->SectorGroup);
	Printf("    Texture: '%s'\n", gameTexture ? gameTexture->GetName().GetChars() : "<nullptr>");
	Printf("    Alpha: %f\n", surface->Alpha);
}

FVector3 RayDir(FAngle angle, FAngle pitch)
{
	auto pc = float(pitch.Cos());
	return FVector3{ pc * float(angle.Cos()), pc * float(angle.Sin()), -float(pitch.Sin()) };
}

DVector3 RayDir(DAngle angle, DAngle pitch)
{
	auto pc = pitch.Cos();
	return DVector3{ pc * (angle.Cos()), pc * (angle.Sin()), -(pitch.Sin()) };
}

CCMD(surfaceinfo)
{
	if (!RequireLevelMesh()) return;

	auto pov = players[consoleplayer].mo;
	if (!pov)
	{
		Printf("players[consoleplayer].mo is null.\n");
		return;
	}

	auto posXYZ = FVector3(pov->Pos());
	posXYZ.Z = float(players[consoleplayer].viewz);
	auto angle = pov->Angles.Yaw;
	auto pitch = pov->Angles.Pitch;

	const auto surface = (DoomLevelMeshSurface*)level.levelMesh->Trace(posXYZ, FVector3(RayDir(angle, pitch)), 32000.0f);
	if (surface)
	{
		PrintSurfaceInfo(surface);
	}
	else
	{
		Printf("No surface was hit.\n");
	}
}

EXTERN_CVAR(Float, lm_scale);

/////////////////////////////////////////////////////////////////////////////

DoomLevelMesh::DoomLevelMesh(FLevelLocals& doomMap)
{
	SunColor = doomMap.SunColor; // TODO keep only one copy?
	SunDirection = doomMap.SunDirection;

	BuildSectorGroups(doomMap);
	CreatePortals(doomMap);

	StaticMesh = std::make_unique<DoomLevelSubmesh>(this, doomMap, true);
	DynamicMesh = std::make_unique<DoomLevelSubmesh>(this, doomMap, false);
}

void DoomLevelMesh::BeginFrame(FLevelLocals& doomMap)
{
	static_cast<DoomLevelSubmesh*>(DynamicMesh.get())->Update(doomMap);
	if (doomMap.lightmaps)
	{
		DynamicMesh->SetupTileTransforms();
		DynamicMesh->PackLightmapAtlas(StaticMesh->LMTextureCount);
	}
}

bool DoomLevelMesh::TraceSky(const FVector3& start, FVector3 direction, float dist)
{
	FVector3 end = start + direction * dist;
	auto surface = Trace(start, direction, dist);
	return surface && surface->IsSky;
}

int DoomLevelMesh::AddSurfaceLights(const LevelMeshSurface* surface, LevelMeshLight* list, int listMaxSize)
{
	FLightNode* node = GetSurfaceLightNode(static_cast<const DoomLevelMeshSurface*>(surface));
	if (!node)
		return 0;

	int listpos = 0;
	while (node && listpos < listMaxSize)
	{
		FDynamicLight* light = node->lightsource;
		if (light && light->Trace())
		{
			DVector3 pos = light->Pos; //light->PosRelative(portalgroup);

			LevelMeshLight& meshlight = list[listpos++];
			meshlight.Origin = { (float)pos.X, (float)pos.Y, (float)pos.Z };
			meshlight.RelativeOrigin = meshlight.Origin;
			meshlight.Radius = (float)light->GetRadius();
			meshlight.Intensity = (float)light->target->Alpha;
			if (light->IsSpot())
			{
				meshlight.InnerAngleCos = (float)light->pSpotInnerAngle->Cos();
				meshlight.OuterAngleCos = (float)light->pSpotOuterAngle->Cos();

				DAngle negPitch = -*light->pPitch;
				DAngle Angle = light->target->Angles.Yaw;
				double xzLen = negPitch.Cos();
				meshlight.SpotDir.X = float(-Angle.Cos() * xzLen);
				meshlight.SpotDir.Y = float(-Angle.Sin() * xzLen);
				meshlight.SpotDir.Z = float(-negPitch.Sin());
			}
			else
			{
				meshlight.InnerAngleCos = -1.0f;
				meshlight.OuterAngleCos = -1.0f;
				meshlight.SpotDir.X = 0.0f;
				meshlight.SpotDir.Y = 0.0f;
				meshlight.SpotDir.Z = 0.0f;
			}
			meshlight.Color.X = light->GetRed() * (1.0f / 255.0f);
			meshlight.Color.Y = light->GetGreen() * (1.0f / 255.0f);
			meshlight.Color.Z = light->GetBlue() * (1.0f / 255.0f);

			if (light->Sector)
				meshlight.SectorGroup = sectorGroup[light->Sector->Index()];
			else
				meshlight.SectorGroup = 0;
		}

		node = node->nextLight;
	}

	return listpos;
}

FLightNode* DoomLevelMesh::GetSurfaceLightNode(const DoomLevelMeshSurface* doomsurf)
{
	FLightNode* node = nullptr;
	if (doomsurf->Type == ST_FLOOR || doomsurf->Type == ST_CEILING)
	{
		node = doomsurf->Subsector->section->lighthead;
	}
	else if (doomsurf->Type == ST_MIDDLESIDE || doomsurf->Type == ST_UPPERSIDE || doomsurf->Type == ST_LOWERSIDE)
	{
		bool isPolyLine = !!(doomsurf->Side->Flags & WALLF_POLYOBJ);
		if (isPolyLine)
		{
			subsector_t* subsector = level.PointInRenderSubsector((doomsurf->Side->V1()->fPos() + doomsurf->Side->V2()->fPos()) * 0.5);
			node = subsector->section->lighthead;
		}
		else if (!doomsurf->ControlSector)
		{
			node = doomsurf->Side->lighthead;
		}
		else // 3d floor needs light from the sidedef on the other side
		{
			int otherside = doomsurf->Side->linedef->sidedef[0] == doomsurf->Side ? 1 : 0;
			node = doomsurf->Side->linedef->sidedef[otherside]->lighthead;
		}
	}
	return node;
}

void DoomLevelMesh::DumpMesh(const FString& objFilename, const FString& mtlFilename) const
{
	DoomLevelSubmesh* submesh = static_cast<DoomLevelSubmesh*>(StaticMesh.get());

	auto f = fopen(objFilename.GetChars(), "w");

	fprintf(f, "# DoomLevelMesh debug export\n");
	fprintf(f, "# Vertices: %u, Indexes: %u, Surfaces: %u\n", submesh->Mesh.Vertices.Size(), submesh->Mesh.Indexes.Size(), submesh->Surfaces.Size());
	fprintf(f, "mtllib %s\n", mtlFilename.GetChars());

	double scale = 1 / 10.0;

	for (const auto& v : submesh->Mesh.Vertices)
	{
		fprintf(f, "v %f %f %f\n", v.x * scale, v.y * scale, v.z * scale);
	}

	for (const auto& v : submesh->Mesh.Vertices)
	{
		fprintf(f, "vt %f %f\n", v.lu, v.lv);
	}

	auto name = [](DoomLevelMeshSurfaceType type) -> const char* {
		switch (type)
		{
		case ST_CEILING:
			return "ceiling";
		case ST_FLOOR:
			return "floor";
		case ST_LOWERSIDE:
			return "lowerside";
		case ST_UPPERSIDE:
			return "upperside";
		case ST_MIDDLESIDE:
			return "middleside";
		case ST_NONE:
			return "none";
		default:
			break;
		}
		return "error";
		};


	uint32_t lastSurfaceIndex = -1;


	bool useErrorMaterial = false;
	int highestUsedAtlasPage = -1;

	for (unsigned i = 0, count = submesh->Mesh.Indexes.Size(); i + 2 < count; i += 3)
	{
		auto index = submesh->Mesh.SurfaceIndexes[i / 3];

		if (index != lastSurfaceIndex)
		{
			lastSurfaceIndex = index;

			if (unsigned(index) >= submesh->Surfaces.Size())
			{
				fprintf(f, "o Surface[%d] (bad index)\n", index);
				fprintf(f, "usemtl error\n");

				useErrorMaterial = true;
			}
			else
			{
				const auto& surface = submesh->Surfaces[index];
				fprintf(f, "o Surface[%d] %s %d%s\n", index, name(surface.Type), surface.TypeIndex, surface.IsSky ? " sky" : "");

				if (surface.LightmapTileIndex >= 0)
				{
					auto& tile = submesh->LightmapTiles[surface.LightmapTileIndex];
					fprintf(f, "usemtl lightmap%d\n", tile.AtlasLocation.ArrayIndex);

					if (tile.AtlasLocation.ArrayIndex > highestUsedAtlasPage)
					{
						highestUsedAtlasPage = tile.AtlasLocation.ArrayIndex;
					}
				}
			}
		}

		// fprintf(f, "f %d %d %d\n", MeshElements[i] + 1, MeshElements[i + 1] + 1, MeshElements[i + 2] + 1);
		fprintf(f, "f %d/%d %d/%d %d/%d\n",
			submesh->Mesh.Indexes[i + 0] + 1, submesh->Mesh.Indexes[i + 0] + 1,
			submesh->Mesh.Indexes[i + 1] + 1, submesh->Mesh.Indexes[i + 1] + 1,
			submesh->Mesh.Indexes[i + 2] + 1, submesh->Mesh.Indexes[i + 2] + 1);

	}

	fclose(f);

	// material

	f = fopen(mtlFilename.GetChars(), "w");

	fprintf(f, "# DoomLevelMesh debug export\n");

	if (useErrorMaterial)
	{
		fprintf(f, "# Surface indices that are referenced, but do not exists in the 'Surface' array\n");
		fprintf(f, "newmtl error\nKa 1 0 0\nKd 1 0 0\nKs 1 0 0\n");
	}

	for (int page = 0; page <= highestUsedAtlasPage; ++page)
	{
		fprintf(f, "newmtl lightmap%d\n", page);
		fprintf(f, "Ka 1 1 1\nKd 1 1 1\nKs 0 0 0\n");
		fprintf(f, "map_Ka lightmap%d.png\n", page);
		fprintf(f, "map_Kd lightmap%d.png\n", page);
	}

	fclose(f);
}

void DoomLevelMesh::BuildSectorGroups(const FLevelLocals& doomMap)
{
	int groupIndex = 0;

	TArray<sector_t*> queue;

	sectorGroup.Resize(doomMap.sectors.Size());
	memset(sectorGroup.Data(), 0, sectorGroup.Size() * sizeof(int));

	for (int i = 0, count = doomMap.sectors.Size(); i < count; ++i)
	{
		auto* sector = &doomMap.sectors[i];

		auto& currentSectorGroup = sectorGroup[sector->Index()];
		if (currentSectorGroup == 0)
		{
			currentSectorGroup = ++groupIndex;

			queue.Push(sector);

			while (queue.Size() > 0)
			{
				auto* sector = queue.Last();
				queue.Pop();

				for (auto& line : sector->Lines)
				{
					auto otherSector = line->frontsector == sector ? line->backsector : line->frontsector;
					if (otherSector && otherSector != sector)
					{
						auto& id = sectorGroup[otherSector->Index()];

						if (id == 0)
						{
							id = groupIndex;
							queue.Push(otherSector);
						}
					}
				}
			}
		}
	}

	if (developer >= 5)
	{
		Printf("DoomLevelMesh::BuildSectorGroups created %d groups.", groupIndex);
	}
}

void DoomLevelMesh::CreatePortals(FLevelLocals& doomMap)
{
	std::map<LevelMeshPortal, int, IdenticalPortalComparator> transformationIndices;
	transformationIndices.emplace(LevelMeshPortal{}, 0); // first portal is an identity matrix

	sectorPortals[0].Resize(doomMap.sectors.Size());
	sectorPortals[1].Resize(doomMap.sectors.Size());

	for (unsigned int i = 0, count = doomMap.sectors.Size(); i < count; i++)
	{
		sector_t* sector = &doomMap.sectors[i];
		for (int plane = 0; plane < 2; plane++)
		{
			auto d = sector->GetPortalDisplacement(plane);
			if (!d.isZero())
			{
				VSMatrix transformation;
				transformation.loadIdentity();
				transformation.translate((float)d.X, (float)d.Y, 0.0f);

				int targetSectorGroup = 0;
				auto portalDestination = sector->GetPortal(plane)->mDestination;
				if (portalDestination)
				{
					targetSectorGroup = sectorGroup[portalDestination->Index()];
				}

				LevelMeshPortal portal;
				portal.transformation = transformation;
				portal.sourceSectorGroup = sectorGroup[i];
				portal.targetSectorGroup = targetSectorGroup;

				auto& index = transformationIndices[portal];
				if (index == 0) // new transformation was created
				{
					index = Portals.Size();
					Portals.Push(portal);
				}

				sectorPortals[plane][i] = index;
			}
			else
			{
				sectorPortals[plane][i] = 0;
			}
		}
	}

	linePortals.Resize(doomMap.lines.Size());
	for (unsigned int i = 0, count = doomMap.lines.Size(); i < count; i++)
	{
		linePortals[i] = 0;

		line_t* sourceLine = &doomMap.lines[i];
		if (sourceLine->isLinePortal())
		{
			VSMatrix transformation;
			transformation.loadIdentity();

			auto targetLine = sourceLine->getPortalDestination();
			if (!targetLine || !sourceLine->frontsector || !targetLine->frontsector)
				continue;

			double z = 0;

			// auto xy = surface.Side->linedef->getPortalDisplacement(); // Works only for static portals... ugh
			auto sourceXYZ = DVector2((sourceLine->v1->fX() + sourceLine->v2->fX()) / 2, (sourceLine->v2->fY() + sourceLine->v1->fY()) / 2);
			auto targetXYZ = DVector2((targetLine->v1->fX() + targetLine->v2->fX()) / 2, (targetLine->v2->fY() + targetLine->v1->fY()) / 2);

			// floor or ceiling alignment
			auto alignment = sourceLine->GetLevel()->linePortals[sourceLine->portalindex].mAlign;
			if (alignment != PORG_ABSOLUTE)
			{
				int plane = alignment == PORG_FLOOR ? 1 : 0;

				auto& sourcePlane = plane ? sourceLine->frontsector->floorplane : sourceLine->frontsector->ceilingplane;
				auto& targetPlane = plane ? targetLine->frontsector->floorplane : targetLine->frontsector->ceilingplane;

				auto tz = targetPlane.ZatPoint(targetXYZ);
				auto sz = sourcePlane.ZatPoint(sourceXYZ);

				z = tz - sz;
			}

			transformation.rotate((float)sourceLine->getPortalAngleDiff().Degrees(), 0.0f, 0.0f, 1.0f);
			transformation.translate((float)(targetXYZ.X - sourceXYZ.X), (float)(targetXYZ.Y - sourceXYZ.Y), (float)z);

			int targetSectorGroup = 0;
			if (auto sector = targetLine->frontsector ? targetLine->frontsector : targetLine->backsector)
			{
				targetSectorGroup = sectorGroup[sector->Index()];
			}

			LevelMeshPortal portal;
			portal.transformation = transformation;
			portal.sourceSectorGroup = sectorGroup[sourceLine->frontsector->Index()];
			portal.targetSectorGroup = targetSectorGroup;

			auto& index = transformationIndices[portal];
			if (index == 0) // new transformation was created
			{
				index = Portals.Size();
				Portals.Push(portal);
			}

			linePortals[i] = index;
		}
	}
}
