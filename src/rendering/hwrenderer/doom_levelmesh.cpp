
#include "templates.h"
#include "doom_levelmesh.h"
#include "g_levellocals.h"
#include "texturemanager.h"
#include "playsim/p_lnspec.h"
#include "c_dispatch.h"
#include "g_levellocals.h"
#include "a_dynlight.h"
#include "common/rendering/vulkan/accelstructs/vk_lightmap.h"
#include <vulkan/accelstructs/halffloat.h>

VSMatrix GetPlaneTextureRotationMatrix(FGameTexture* gltexture, const sector_t* sector, int plane);
void GetTexCoordInfo(FGameTexture* tex, FTexCoordInfo* tci, side_t* side, int texpos);

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
	auto stats = levelMesh->GatherSurfacePixelStats();

	out.Format("Surfaces: %u (sky: %u, awaiting updates: %u)\nSurface pixel area to update: %u\nSurface pixel area: %u\nAtlas pixel area:   %u\nAtlas efficiency: %.4f%%",
		stats.surfaces.total, stats.surfaces.sky, std::max(stats.surfaces.dirty - stats.surfaces.sky, (uint32_t)0),
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
	for (auto& surface : static_cast<DoomLevelSubmesh*>(level.levelMesh->StaticMesh.get())->Surfaces)
	{
		if (!surface.needsUpdate)
			++count;
		surface.needsUpdate = true;
	}
	Printf("Marked %d out of %d surfaces for update.\n", count, level.levelMesh->StaticMesh->GetSurfaceCount());
}

void PrintSurfaceInfo(const DoomLevelMeshSurface* surface)
{
	if (!RequireLevelMesh()) return;

	auto gameTexture = TexMan.GameByIndex(surface->texture.GetIndex());

	Printf("Surface %d (%p)\n    Type: %d, TypeIndex: %d, ControlSector: %d\n", surface->Submesh->GetSurfaceIndex(surface), surface, surface->Type, surface->TypeIndex, surface->ControlSector ? surface->ControlSector->Index() : -1);
	Printf("    Atlas page: %d, x:%d, y:%d\n", surface->AtlasTile.ArrayIndex, surface->AtlasTile.X, surface->AtlasTile.Y);
	Printf("    Pixels: %dx%d (area: %d)\n", surface->AtlasTile.Width, surface->AtlasTile.Height, surface->Area());
	Printf("    Sample dimension: %d\n", surface->sampleDimension);
	Printf("    Needs update?: %d\n", surface->needsUpdate);
	Printf("    Sector group: %d\n", surface->sectorGroup);
	Printf("    Texture: '%s' (id=%d)\n", gameTexture ? gameTexture->GetName().GetChars() : "<nullptr>", surface->texture.GetIndex());
	Printf("    Alpha: %f\n", surface->alpha);
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

	StaticMesh = std::make_unique<DoomLevelSubmesh>();
	DynamicMesh = std::make_unique<DoomLevelSubmesh>();

	static_cast<DoomLevelSubmesh*>(StaticMesh.get())->CreateStatic(doomMap);
	static_cast<DoomLevelSubmesh*>(DynamicMesh.get())->CreateDynamic(doomMap);
}

void DoomLevelMesh::BeginFrame(FLevelLocals& doomMap)
{
	static_cast<DoomLevelSubmesh*>(DynamicMesh.get())->UpdateDynamic(doomMap, static_cast<DoomLevelSubmesh*>(StaticMesh.get())->LMTextureCount);
}

bool DoomLevelMesh::TraceSky(const FVector3& start, FVector3 direction, float dist)
{
	FVector3 end = start + direction * dist;
	auto surface = Trace(start, direction, dist);
	return surface && surface->bSky;
}

void DoomLevelMesh::PackLightmapAtlas()
{
	static_cast<DoomLevelSubmesh*>(StaticMesh.get())->PackLightmapAtlas(0);
}

void DoomLevelMesh::BindLightmapSurfacesToGeometry(FLevelLocals& doomMap)
{
	static_cast<DoomLevelSubmesh*>(StaticMesh.get())->BindLightmapSurfacesToGeometry(doomMap);
}

void DoomLevelMesh::DisableLightmaps()
{
	static_cast<DoomLevelSubmesh*>(StaticMesh.get())->DisableLightmaps();
}

void DoomLevelMesh::DumpMesh(const FString& objFilename, const FString& mtlFilename) const
{
	static_cast<DoomLevelSubmesh*>(StaticMesh.get())->DumpMesh(objFilename, mtlFilename);
}

int DoomLevelMesh::AddSurfaceLights(const LevelMeshSurface* surface, LevelMeshLight* list, int listMaxSize)
{
	const DoomLevelMeshSurface* doomsurf = static_cast<const DoomLevelMeshSurface*>(surface);

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
				meshlight.SectorGroup = static_cast<DoomLevelSubmesh*>(StaticMesh.get())->sectorGroup[light->Sector->Index()];
			else
				meshlight.SectorGroup = 0;
		}

		node = node->nextLight;
	}

	return listpos;
}

/////////////////////////////////////////////////////////////////////////////

void DoomLevelSubmesh::CreateStatic(FLevelLocals& doomMap)
{
	MeshVertices.Clear();
	MeshVertexUVs.Clear();
	MeshElements.Clear();

	LightmapSampleDistance = doomMap.LightmapSampleDistance;

	BuildSectorGroups(doomMap);

	for (unsigned int i = 0; i < doomMap.sides.Size(); i++)
	{
		bool isPolyLine = !!(doomMap.sides[i].Flags & WALLF_POLYOBJ);
		if (!isPolyLine)
			CreateSideSurfaces(doomMap, &doomMap.sides[i]);
	}

	CreateSubsectorSurfaces(doomMap);

	CreateIndexes();
	SetupLightmapUvs(doomMap);
	BuildTileSurfaceLists();
	UpdateCollision();
}

void DoomLevelSubmesh::CreateDynamic(FLevelLocals& doomMap)
{
	LightmapSampleDistance = doomMap.LightmapSampleDistance;
	BuildSectorGroups(doomMap);
}

void DoomLevelSubmesh::UpdateDynamic(FLevelLocals& doomMap, int lightmapStartIndex)
{
	Surfaces.Clear();
	MeshVertices.Clear();
	MeshVertexUVs.Clear();
	MeshElements.Clear();
	MeshSurfaceIndexes.Clear();
	LightmapUvs.Clear();

	// Look for polyobjects
	for (unsigned int i = 0; i < doomMap.lines.Size(); i++)
	{
		side_t* side = doomMap.lines[i].sidedef[0];
		bool isPolyLine = !!(side->Flags & WALLF_POLYOBJ);
		if (isPolyLine)
		{
			// Make sure we have a lightmap array on the polyobj sidedef
			if (!side->lightmap)
			{
				auto array = std::make_unique<DoomLevelMeshSurface*[]>(4);
				memset(array.get(), 0, sizeof(DoomLevelMeshSurface*));
				side->lightmap = array.get();
				PolyLMSurfaces.Push(std::move(array));
			}

			CreateSideSurfaces(doomMap, side);
		}
	}

	CreateIndexes();
	SetupLightmapUvs(doomMap);
	BuildTileSurfaceLists();
	UpdateCollision();

	PackLightmapAtlas(lightmapStartIndex);
	BindLightmapSurfacesToGeometry(doomMap);
}

void DoomLevelSubmesh::CreateIndexes()
{
	for (size_t i = 0; i < Surfaces.Size(); i++)
	{
		DoomLevelMeshSurface& s = Surfaces[i];
		int numVerts = s.numVerts;
		unsigned int pos = s.startVertIndex;
		FVector3* verts = &MeshVertices[pos];

		s.startElementIndex = MeshElements.Size();
		s.numElements = 0;

		if (s.Type == ST_FLOOR || s.Type == ST_CEILING)
		{
			for (int j = 2; j < numVerts; j++)
			{
				if (!IsDegenerate(verts[0], verts[j - 1], verts[j]))
				{
					MeshElements.Push(pos);
					MeshElements.Push(pos + j - 1);
					MeshElements.Push(pos + j);
					MeshSurfaceIndexes.Push((int)i);
					s.numElements += 3;
				}
			}
		}
		else if (s.Type == ST_MIDDLESIDE || s.Type == ST_UPPERSIDE || s.Type == ST_LOWERSIDE)
		{
			if (!IsDegenerate(verts[0], verts[1], verts[2]))
			{
				MeshElements.Push(pos + 0);
				MeshElements.Push(pos + 1);
				MeshElements.Push(pos + 2);
				MeshSurfaceIndexes.Push((int)i);
				s.numElements += 3;
			}
			if (!IsDegenerate(verts[1], verts[2], verts[3]))
			{
				MeshElements.Push(pos + 3);
				MeshElements.Push(pos + 2);
				MeshElements.Push(pos + 1);
				MeshSurfaceIndexes.Push((int)i);
				s.numElements += 3;
			}
		}
	}
}

void DoomLevelSubmesh::BuildSectorGroups(const FLevelLocals& doomMap)
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

void DoomLevelSubmesh::CreatePortals()
{
	std::map<LevelMeshPortal, int, IdenticalPortalComparator> transformationIndices; // TODO use the list of portals from the level to avoids duplicates?
	transformationIndices.emplace(LevelMeshPortal{}, 0); // first portal is an identity matrix

	for (auto& surface : Surfaces)
	{
		bool hasPortal = [&]() {
			if (surface.Type == ST_FLOOR || surface.Type == ST_CEILING)
			{
				return !surface.Subsector->sector->GetPortalDisplacement(surface.Type == ST_FLOOR ? sector_t::floor : sector_t::ceiling).isZero();
			}
			else if (surface.Type == ST_MIDDLESIDE)
			{
				return surface.Side->linedef->isLinePortal();
			}
			return false; // It'll take eternity to get lower/upper side portals into the ZDoom family.
		}();

		if (hasPortal)
		{
			auto transformation = [&]() {
				VSMatrix matrix;
				matrix.loadIdentity();

				if (surface.Type == ST_FLOOR || surface.Type == ST_CEILING)
				{
					auto d = surface.Subsector->sector->GetPortalDisplacement(surface.Type == ST_FLOOR ? sector_t::floor : sector_t::ceiling);
					matrix.translate((float)d.X, (float)d.Y, 0.0f);
				}
				else if(surface.Type == ST_MIDDLESIDE)
				{
					auto sourceLine = surface.Side->linedef;

					if (sourceLine->isLinePortal())
					{
						auto targetLine = sourceLine->getPortalDestination();
						if (targetLine && sourceLine->frontsector && targetLine->frontsector)
						{
							double z = 0;

							// auto xy = surface.Side->linedef->getPortalDisplacement(); // Works only for static portals... ugh
							auto sourceXYZ = DVector2((sourceLine->v1->fX() + sourceLine->v2->fX()) / 2, (sourceLine->v2->fY() + sourceLine->v1->fY()) / 2);
							auto targetXYZ = DVector2((targetLine->v1->fX() + targetLine->v2->fX()) / 2, (targetLine->v2->fY() + targetLine->v1->fY()) / 2);

							// floor or ceiling alignment
							auto alignment = surface.Side->linedef->GetLevel()->linePortals[surface.Side->linedef->portalindex].mAlign;
							if (alignment != PORG_ABSOLUTE)
							{
								int plane = alignment == PORG_FLOOR ? 1 : 0;

								auto& sourcePlane = plane ? sourceLine->frontsector->floorplane : sourceLine->frontsector->ceilingplane;
								auto& targetPlane = plane ? targetLine->frontsector->floorplane : targetLine->frontsector->ceilingplane;

								auto tz = targetPlane.ZatPoint(targetXYZ);
								auto sz = sourcePlane.ZatPoint(sourceXYZ);

								z = tz - sz;
							}

							matrix.rotate((float)sourceLine->getPortalAngleDiff().Degrees(), 0.0f, 0.0f, 1.0f);
							matrix.translate((float)(targetXYZ.X - sourceXYZ.X), (float)(targetXYZ.Y - sourceXYZ.Y), (float)z);
						}
					}
				}
				return matrix;
			}();

			LevelMeshPortal portal;
			portal.transformation = transformation;
			portal.sourceSectorGroup = surface.sectorGroup;
			portal.targetSectorGroup = [&]() {
				if (surface.Type == ST_FLOOR || surface.Type == ST_CEILING)
				{
					auto plane = surface.Type == ST_FLOOR ? sector_t::floor : sector_t::ceiling;
					auto portalDestination = surface.Subsector->sector->GetPortal(plane)->mDestination;
					if (portalDestination)
					{
						return sectorGroup[portalDestination->Index()];
					}
				}
				else if (surface.Type == ST_MIDDLESIDE)
				{
					auto targetLine = surface.Side->linedef->getPortalDestination();
					auto sector = targetLine->frontsector ? targetLine->frontsector : targetLine->backsector;
					if (sector)
					{
						return sectorGroup[sector->Index()];
					}
				}
				return 0;
			}();

			auto& index = transformationIndices[portal];

			if (index == 0) // new transformation was created
			{
				index = Portals.Size();
				Portals.Push(portal);
			}

			surface.portalIndex = index;
		}
		else
		{
			surface.portalIndex = 0;
		}
	}
}

void DoomLevelSubmesh::BindLightmapSurfacesToGeometry(FLevelLocals& doomMap)
{
	// You have no idea how long this took me to figure out...

	// Reorder vertices into renderer format
	for (DoomLevelMeshSurface& surface : Surfaces)
	{
		if (surface.Type == ST_FLOOR)
		{
			// reverse vertices on floor
			for (int j = surface.startUvIndex + surface.numVerts - 1, k = surface.startUvIndex; j > k; j--, k++)
			{
				std::swap(LightmapUvs[k], LightmapUvs[j]);
			}
		}
		else if (surface.Type != ST_CEILING) // walls
		{
			// from 0 1 2 3
			// to   0 2 1 3
			std::swap(LightmapUvs[surface.startUvIndex + 1], LightmapUvs[surface.startUvIndex + 2]);
			std::swap(LightmapUvs[surface.startUvIndex + 2], LightmapUvs[surface.startUvIndex + 3]);
		}

		surface.TexCoords = (float*)&LightmapUvs[surface.startUvIndex];
	}

	// Link surfaces
	for (auto& surface : Surfaces)
	{
		if (surface.Type == ST_FLOOR || surface.Type == ST_CEILING)
		{
			surface.Subsector = &doomMap.subsectors[surface.TypeIndex];
			if (surface.Subsector->firstline && surface.Subsector->firstline->sidedef)
				surface.Subsector->firstline->sidedef->sector->HasLightmaps = true;
			SetSubsectorLightmap(&surface);
		}
		else
		{
			surface.Side = &doomMap.sides[surface.TypeIndex];
			SetSideLightmap(&surface);
		}
	}
}

void DoomLevelSubmesh::SetSubsectorLightmap(DoomLevelMeshSurface* surface)
{
	if (!surface->ControlSector)
	{
		int index = surface->Type == ST_CEILING ? 1 : 0;
		surface->Subsector->lightmap[index][0] = surface;
	}
	else
	{
		int index = surface->Type == ST_CEILING ? 0 : 1;
		const auto& ffloors = surface->Subsector->sector->e->XFloor.ffloors;
		for (unsigned int i = 0; i < ffloors.Size(); i++)
		{
			if (ffloors[i]->model == surface->ControlSector)
			{
				surface->Subsector->lightmap[index][i + 1] = surface;
			}
		}
	}
}

void DoomLevelSubmesh::SetSideLightmap(DoomLevelMeshSurface* surface)
{
	if (!surface->ControlSector)
	{
		if (surface->Type == ST_UPPERSIDE)
		{
			surface->Side->lightmap[0] = surface;
		}
		else if (surface->Type == ST_MIDDLESIDE)
		{
			surface->Side->lightmap[1] = surface;
			surface->Side->lightmap[2] = surface;
		}
		else if (surface->Type == ST_LOWERSIDE)
		{
			surface->Side->lightmap[3] = surface;
		}
	}
	else
	{
		const auto& ffloors = surface->Side->sector->e->XFloor.ffloors;
		for (unsigned int i = 0; i < ffloors.Size(); i++)
		{
			if (ffloors[i]->model == surface->ControlSector)
			{
				surface->Side->lightmap[4 + i] = surface;
			}
		}
	}
}

void DoomLevelSubmesh::CreateLinePortalSurface(FLevelLocals& doomMap, side_t* side)
{
	sector_t* front = side->sector;

	FVector2 v1 = ToFVector2(side->V1()->fPos());
	FVector2 v2 = ToFVector2(side->V2()->fPos());

	float v1Top = (float)front->ceilingplane.ZatPoint(v1);
	float v1Bottom = (float)front->floorplane.ZatPoint(v1);
	float v2Top = (float)front->ceilingplane.ZatPoint(v2);
	float v2Bottom = (float)front->floorplane.ZatPoint(v2);

	FVector3 verts[4];
	verts[0].X = verts[2].X = v1.X;
	verts[0].Y = verts[2].Y = v1.Y;
	verts[1].X = verts[3].X = v2.X;
	verts[1].Y = verts[3].Y = v2.Y;
	verts[0].Z = v1Bottom;
	verts[1].Z = v2Bottom;
	verts[2].Z = v1Top;
	verts[3].Z = v2Top;

	DoomLevelMeshSurface surf;
	surf.Submesh = this;
	surf.Type = ST_MIDDLESIDE;
	surf.TypeIndex = side->Index();
	surf.bSky = front->GetTexture(sector_t::floor) == skyflatnum || front->GetTexture(sector_t::ceiling) == skyflatnum;
	surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;
	surf.startVertIndex = MeshVertices.Size();
	surf.numVerts = 4;

	MeshVertices.Push(verts[0]);
	MeshVertices.Push(verts[1]);
	MeshVertices.Push(verts[2]);
	MeshVertices.Push(verts[3]);

	surf.plane = ToPlane(verts[0], verts[1], verts[2], verts[3]);
	surf.sectorGroup = sectorGroup[front->Index()];

	SetSideTextureUVs(surf, side, side_t::top, v1Top, v1Bottom, v2Top, v2Bottom);

	Surfaces.Push(surf);
}

void DoomLevelSubmesh::CreateSideSurfaces(FLevelLocals& doomMap, side_t* side)
{
	sector_t* front = side->sector;
	sector_t* back = (side->linedef->frontsector == front) ? side->linedef->backsector : side->linedef->frontsector;

	FVector2 v1 = ToFVector2(side->V1()->fPos());
	FVector2 v2 = ToFVector2(side->V2()->fPos());

	float v1Top = (float)front->ceilingplane.ZatPoint(v1);
	float v1Bottom = (float)front->floorplane.ZatPoint(v1);
	float v2Top = (float)front->ceilingplane.ZatPoint(v2);
	float v2Bottom = (float)front->floorplane.ZatPoint(v2);

	if (side->linedef->getPortal() && side->linedef->frontsector == front)
	{
		CreateLinePortalSurface(doomMap, side);
	}
	else if (side->linedef->special == Line_Horizon && front != back)
	{
		CreateLineHorizonSurface(doomMap, side);
	}
	else if (!back)
	{
		CreateFrontWallSurface(doomMap, side);
	}
	else
	{
		if (side->textures[side_t::mid].texture.isValid())
		{
			CreateMidWallSurface(doomMap, side);
		}

		Create3DFloorWallSurfaces(doomMap, side);

		float v1TopBack = (float)back->ceilingplane.ZatPoint(v1);
		float v1BottomBack = (float)back->floorplane.ZatPoint(v1);
		float v2TopBack = (float)back->ceilingplane.ZatPoint(v2);
		float v2BottomBack = (float)back->floorplane.ZatPoint(v2);

		if (v1Bottom < v1BottomBack || v2Bottom < v2BottomBack)
		{
			CreateBottomWallSurface(doomMap, side);
		}

		if (v1Top > v1TopBack || v2Top > v2TopBack)
		{
			CreateTopWallSurface(doomMap, side);
		}
	}
}

void DoomLevelSubmesh::CreateLineHorizonSurface(FLevelLocals& doomMap, side_t* side)
{
	sector_t* front = side->sector;

	FVector2 v1 = ToFVector2(side->V1()->fPos());
	FVector2 v2 = ToFVector2(side->V2()->fPos());

	float v1Top = (float)front->ceilingplane.ZatPoint(v1);
	float v1Bottom = (float)front->floorplane.ZatPoint(v1);
	float v2Top = (float)front->ceilingplane.ZatPoint(v2);
	float v2Bottom = (float)front->floorplane.ZatPoint(v2);

	DoomLevelMeshSurface surf;
	surf.Submesh = this;
	surf.Type = ST_MIDDLESIDE;
	surf.TypeIndex = side->Index();
	surf.bSky = front->GetTexture(sector_t::floor) == skyflatnum || front->GetTexture(sector_t::ceiling) == skyflatnum;
	surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;

	FVector3 verts[4];
	verts[0].X = verts[2].X = v1.X;
	verts[0].Y = verts[2].Y = v1.Y;
	verts[1].X = verts[3].X = v2.X;
	verts[1].Y = verts[3].Y = v2.Y;
	verts[0].Z = v1Bottom;
	verts[1].Z = v2Bottom;
	verts[2].Z = v1Top;
	verts[3].Z = v2Top;

	surf.startVertIndex = MeshVertices.Size();
	surf.numVerts = 4;
	MeshVertices.Push(verts[0]);
	MeshVertices.Push(verts[1]);
	MeshVertices.Push(verts[2]);
	MeshVertices.Push(verts[3]);

	surf.plane = ToPlane(verts[0], verts[1], verts[2], verts[3]);
	surf.sectorGroup = sectorGroup[front->Index()];

	SetSideTextureUVs(surf, side, side_t::top, v1Top, v1Bottom, v2Top, v2Bottom);

	Surfaces.Push(surf);
}

void DoomLevelSubmesh::CreateFrontWallSurface(FLevelLocals& doomMap, side_t* side)
{
	sector_t* front = side->sector;
/*
	bool isPolyLine = !!(side->Flags & WALLF_POLYOBJ);
	if (isPolyLine)
	{
		subsector_t* subsector = level.PointInRenderSubsector((side->V1()->fPos() + side->V2()->fPos()) * 0.5);
		front = subsector->sector;
	}
*/
	FVector2 v1 = ToFVector2(side->V1()->fPos());
	FVector2 v2 = ToFVector2(side->V2()->fPos());

	float v1Top = (float)front->ceilingplane.ZatPoint(v1);
	float v1Bottom = (float)front->floorplane.ZatPoint(v1);
	float v2Top = (float)front->ceilingplane.ZatPoint(v2);
	float v2Bottom = (float)front->floorplane.ZatPoint(v2);

	FVector3 verts[4];
	verts[0].X = verts[2].X = v1.X;
	verts[0].Y = verts[2].Y = v1.Y;
	verts[1].X = verts[3].X = v2.X;
	verts[1].Y = verts[3].Y = v2.Y;
	verts[0].Z = v1Bottom;
	verts[1].Z = v2Bottom;
	verts[2].Z = v1Top;
	verts[3].Z = v2Top;

	DoomLevelMeshSurface surf;
	surf.Submesh = this;
	surf.bSky = false;
	surf.startVertIndex = MeshVertices.Size();
	surf.numVerts = 4;
	surf.bSky = false;
	MeshVertices.Push(verts[0]);
	MeshVertices.Push(verts[1]);
	MeshVertices.Push(verts[2]);
	MeshVertices.Push(verts[3]);

	surf.plane = ToPlane(verts[0], verts[1], verts[2], verts[3]);
	surf.Type = ST_MIDDLESIDE;
	surf.TypeIndex = side->Index();
	surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;
	surf.ControlSector = nullptr;
	surf.sectorGroup = sectorGroup[front->Index()];
	surf.texture = side->textures[side_t::mid].texture;

	SetSideTextureUVs(surf, side, side_t::top, v1Top, v1Bottom, v2Top, v2Bottom);

	Surfaces.Push(surf);
}

void DoomLevelSubmesh::CreateMidWallSurface(FLevelLocals& doomMap, side_t* side)
{
	sector_t* front = side->sector;

	FVector2 v1 = ToFVector2(side->V1()->fPos());
	FVector2 v2 = ToFVector2(side->V2()->fPos());

	float v1Top = (float)front->ceilingplane.ZatPoint(v1);
	float v1Bottom = (float)front->floorplane.ZatPoint(v1);
	float v2Top = (float)front->ceilingplane.ZatPoint(v2);
	float v2Bottom = (float)front->floorplane.ZatPoint(v2);

	FVector3 verts[4];
	verts[0].X = verts[2].X = v1.X;
	verts[0].Y = verts[2].Y = v1.Y;
	verts[1].X = verts[3].X = v2.X;
	verts[1].Y = verts[3].Y = v2.Y;

	const auto& texture = side->textures[side_t::mid].texture;

	if ((side->Flags & WALLF_WRAP_MIDTEX) || (side->linedef->flags & WALLF_WRAP_MIDTEX))
	{
		verts[0].Z = v1Bottom;
		verts[1].Z = v2Bottom;
		verts[2].Z = v1Top;
		verts[3].Z = v2Top;
	}
	else
	{
		int offset = 0;

		auto gameTexture = TexMan.GetGameTexture(texture);

		float mid1Top = (float)(gameTexture->GetDisplayHeight() / side->textures[side_t::mid].yScale);
		float mid2Top = (float)(gameTexture->GetDisplayHeight() / side->textures[side_t::mid].yScale);
		float mid1Bottom = 0;
		float mid2Bottom = 0;

		float yTextureOffset = (float)(side->textures[side_t::mid].yOffset / gameTexture->GetScaleY());

		if (side->linedef->flags & ML_DONTPEGBOTTOM)
		{
			yTextureOffset += (float)side->sector->planes[sector_t::floor].TexZ;
		}
		else
		{
			yTextureOffset += (float)(side->sector->planes[sector_t::ceiling].TexZ - gameTexture->GetDisplayHeight() / side->textures[side_t::mid].yScale);
		}

		verts[0].Z = min(max(yTextureOffset + mid1Bottom, v1Bottom), v1Top);
		verts[1].Z = min(max(yTextureOffset + mid2Bottom, v2Bottom), v2Top);
		verts[2].Z = max(min(yTextureOffset + mid1Top, v1Top), v1Bottom);
		verts[3].Z = max(min(yTextureOffset + mid2Top, v2Top), v2Bottom);
	}

	// mid texture
	DoomLevelMeshSurface surf;
	surf.Submesh = this;
	surf.bSky = false;
	surf.startVertIndex = MeshVertices.Size();
	surf.numVerts = 4;
	surf.bSky = false;
	surf.plane = ToPlane(verts[0], verts[1], verts[2], verts[3]);

	FVector3 offset = surf.plane.XYZ() * 0.05f; // for better accuracy when raytracing mid-textures from each side

	if (side->linedef->sidedef[0] != side)
	{
		surf.plane = -surf.plane;
		surf.plane.W = -surf.plane.W;
	}

	MeshVertices.Push(verts[0] + offset);
	MeshVertices.Push(verts[1] + offset);
	MeshVertices.Push(verts[2] + offset);
	MeshVertices.Push(verts[3] + offset);

	surf.Type = ST_MIDDLESIDE;
	surf.TypeIndex = side->Index();
	surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;
	surf.ControlSector = nullptr;
	surf.sectorGroup = sectorGroup[front->Index()];
	surf.texture = texture;
	surf.alpha = float(side->linedef->alpha);

	SetSideTextureUVs(surf, side, side_t::top, verts[2].Z, verts[0].Z, verts[3].Z, verts[1].Z);

	Surfaces.Push(surf);
}

void DoomLevelSubmesh::Create3DFloorWallSurfaces(FLevelLocals& doomMap, side_t* side)
{
	sector_t* front = side->sector;
	sector_t* back = (side->linedef->frontsector == front) ? side->linedef->backsector : side->linedef->frontsector;

	FVector2 v1 = ToFVector2(side->V1()->fPos());
	FVector2 v2 = ToFVector2(side->V2()->fPos());

	float v1Top = (float)front->ceilingplane.ZatPoint(v1);
	float v1Bottom = (float)front->floorplane.ZatPoint(v1);
	float v2Top = (float)front->ceilingplane.ZatPoint(v2);
	float v2Bottom = (float)front->floorplane.ZatPoint(v2);

	for (unsigned int j = 0; j < front->e->XFloor.ffloors.Size(); j++)
	{
		F3DFloor* xfloor = front->e->XFloor.ffloors[j];

		// Don't create a line when both sectors have the same 3d floor
		bool bothSides = false;
		for (unsigned int k = 0; k < back->e->XFloor.ffloors.Size(); k++)
		{
			if (back->e->XFloor.ffloors[k] == xfloor)
			{
				bothSides = true;
				break;
			}
		}
		if (bothSides)
			continue;

		DoomLevelMeshSurface surf;
		surf.Submesh = this;
		surf.Type = ST_MIDDLESIDE;
		surf.TypeIndex = side->Index();
		surf.ControlSector = xfloor->model;
		surf.bSky = false;
		surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;

		float blZ = (float)xfloor->model->floorplane.ZatPoint(v1);
		float brZ = (float)xfloor->model->floorplane.ZatPoint(v2);
		float tlZ = (float)xfloor->model->ceilingplane.ZatPoint(v1);
		float trZ = (float)xfloor->model->ceilingplane.ZatPoint(v2);

		FVector3 verts[4];
		verts[0].X = verts[2].X = v2.X;
		verts[0].Y = verts[2].Y = v2.Y;
		verts[1].X = verts[3].X = v1.X;
		verts[1].Y = verts[3].Y = v1.Y;
		verts[0].Z = brZ;
		verts[1].Z = blZ;
		verts[2].Z = trZ;
		verts[3].Z = tlZ;

		surf.startVertIndex = MeshVertices.Size();
		surf.numVerts = 4;
		MeshVertices.Push(verts[0]);
		MeshVertices.Push(verts[1]);
		MeshVertices.Push(verts[2]);
		MeshVertices.Push(verts[3]);

		surf.plane = ToPlane(verts[0], verts[1], verts[2], verts[3]);
		surf.sectorGroup = sectorGroup[front->Index()];
		surf.texture = side->textures[side_t::mid].texture;

		SetSideTextureUVs(surf, side, side_t::top, tlZ, blZ, trZ, brZ);

		Surfaces.Push(surf);
	}
}

void DoomLevelSubmesh::CreateTopWallSurface(FLevelLocals& doomMap, side_t* side)
{
	sector_t* front = side->sector;
	sector_t* back = (side->linedef->frontsector == front) ? side->linedef->backsector : side->linedef->frontsector;

	FVector2 v1 = ToFVector2(side->V1()->fPos());
	FVector2 v2 = ToFVector2(side->V2()->fPos());

	float v1Top = (float)front->ceilingplane.ZatPoint(v1);
	float v2Top = (float)front->ceilingplane.ZatPoint(v2);
	float v1TopBack = (float)back->ceilingplane.ZatPoint(v1);
	float v2TopBack = (float)back->ceilingplane.ZatPoint(v2);

	bool bSky = IsTopSideSky(front, back, side);
	if (!bSky && !IsTopSideVisible(side))
		return;

	FVector3 verts[4];
	verts[0].X = verts[2].X = v1.X;
	verts[0].Y = verts[2].Y = v1.Y;
	verts[1].X = verts[3].X = v2.X;
	verts[1].Y = verts[3].Y = v2.Y;
	verts[0].Z = v1TopBack;
	verts[1].Z = v2TopBack;
	verts[2].Z = v1Top;
	verts[3].Z = v2Top;

	DoomLevelMeshSurface surf;
	surf.Submesh = this;
	surf.startVertIndex = MeshVertices.Size();
	surf.numVerts = 4;
	MeshVertices.Push(verts[0]);
	MeshVertices.Push(verts[1]);
	MeshVertices.Push(verts[2]);
	MeshVertices.Push(verts[3]);

	surf.plane = ToPlane(verts[0], verts[1], verts[2], verts[3]);
	surf.Type = ST_UPPERSIDE;
	surf.TypeIndex = side->Index();
	surf.bSky = bSky;
	surf.sampleDimension = side->textures[side_t::top].LightmapSampleDistance;
	surf.ControlSector = nullptr;
	surf.sectorGroup = sectorGroup[front->Index()];
	surf.texture = side->textures[side_t::top].texture;

	SetSideTextureUVs(surf, side, side_t::top, v1Top, v1TopBack, v2Top, v2TopBack);

	Surfaces.Push(surf);
}

void DoomLevelSubmesh::CreateBottomWallSurface(FLevelLocals& doomMap, side_t* side)
{
	if (!IsBottomSideVisible(side))
		return;

	sector_t* front = side->sector;
	sector_t* back = (side->linedef->frontsector == front) ? side->linedef->backsector : side->linedef->frontsector;

	FVector2 v1 = ToFVector2(side->V1()->fPos());
	FVector2 v2 = ToFVector2(side->V2()->fPos());

	float v1Bottom = (float)front->floorplane.ZatPoint(v1);
	float v2Bottom = (float)front->floorplane.ZatPoint(v2);
	float v1BottomBack = (float)back->floorplane.ZatPoint(v1);
	float v2BottomBack = (float)back->floorplane.ZatPoint(v2);

	FVector3 verts[4];
	verts[0].X = verts[2].X = v1.X;
	verts[0].Y = verts[2].Y = v1.Y;
	verts[1].X = verts[3].X = v2.X;
	verts[1].Y = verts[3].Y = v2.Y;
	verts[0].Z = v1Bottom;
	verts[1].Z = v2Bottom;
	verts[2].Z = v1BottomBack;
	verts[3].Z = v2BottomBack;

	DoomLevelMeshSurface surf;
	surf.Submesh = this;
	surf.startVertIndex = MeshVertices.Size();
	surf.numVerts = 4;
	MeshVertices.Push(verts[0]);
	MeshVertices.Push(verts[1]);
	MeshVertices.Push(verts[2]);
	MeshVertices.Push(verts[3]);

	surf.plane = ToPlane(verts[0], verts[1], verts[2], verts[3]);
	surf.Type = ST_LOWERSIDE;
	surf.TypeIndex = side->Index();
	surf.bSky = false;
	surf.sampleDimension = side->textures[side_t::bottom].LightmapSampleDistance;
	surf.ControlSector = nullptr;
	surf.sectorGroup = sectorGroup[front->Index()];
	surf.texture = side->textures[side_t::bottom].texture;

	SetSideTextureUVs(surf, side, side_t::bottom, v1BottomBack, v1Bottom, v2BottomBack, v2Bottom);

	Surfaces.Push(surf);
}

void DoomLevelSubmesh::SetSideTextureUVs(DoomLevelMeshSurface& surface, side_t* side, side_t::ETexpart texpart, float v1TopZ, float v1BottomZ, float v2TopZ, float v2BottomZ)
{
	MeshVertexUVs.Reserve(4);
	FVector2* uvs = &MeshVertexUVs[surface.startVertIndex];

	if (surface.texture.isValid())
	{
		const auto gtxt = TexMan.GetGameTexture(surface.texture);

		FTexCoordInfo tci;
		GetTexCoordInfo(gtxt, &tci, side, texpart);

		float startU = tci.FloatToTexU(tci.TextureOffset((float)side->GetTextureXOffset(texpart)) + tci.TextureOffset((float)side->GetTextureXOffset(texpart)));
		float endU = startU + tci.FloatToTexU(side->TexelLength);

		uvs[0].X = startU;
		uvs[1].X = endU;
		uvs[2].X = startU;
		uvs[3].X = endU;

		// To do: the ceiling version is apparently used in some situation related to 3d floors (rover->top.isceiling)
		//float offset = tci.RowOffset((float)side->GetTextureYOffset(texpart)) + tci.RowOffset((float)side->GetTextureYOffset(texpart)) + (float)side->sector->GetPlaneTexZ(sector_t::ceiling);
		float offset = tci.RowOffset((float)side->GetTextureYOffset(texpart)) + tci.RowOffset((float)side->GetTextureYOffset(texpart)) + (float)side->sector->GetPlaneTexZ(sector_t::floor);

		uvs[0].Y = tci.FloatToTexV(offset - v1BottomZ);
		uvs[1].Y = tci.FloatToTexV(offset - v2BottomZ);
		uvs[2].Y = tci.FloatToTexV(offset - v1TopZ);
		uvs[3].Y = tci.FloatToTexV(offset - v2TopZ);
	}
	else
	{
		uvs[0] = FVector2(0.0f, 0.0f);
		uvs[1] = FVector2(0.0f, 0.0f);
		uvs[2] = FVector2(0.0f, 0.0f);
		uvs[3] = FVector2(0.0f, 0.0f);
	}
}

void DoomLevelSubmesh::CreateFloorSurface(FLevelLocals &doomMap, subsector_t *sub, sector_t *sector, sector_t *controlSector, int typeIndex)
{
	DoomLevelMeshSurface surf;
	surf.Submesh = this;

	secplane_t plane;
	if (!controlSector)
	{
		plane = sector->floorplane;
		surf.bSky = IsSkySector(sector, sector_t::floor);
	}
	else
	{
		plane = controlSector->ceilingplane;
		plane.FlipVert();
		surf.bSky = false;
	}

	surf.numVerts = sub->numlines;
	surf.startVertIndex = MeshVertices.Size();
	surf.texture = (controlSector ? controlSector : sector)->planes[sector_t::floor].Texture;

	FGameTexture* txt = TexMan.GetGameTexture(surf.texture);
	float w = txt->GetDisplayWidth();
	float h = txt->GetDisplayHeight();
	VSMatrix mat = GetPlaneTextureRotationMatrix(txt, sector, sector_t::floor);

	MeshVertices.Resize(surf.startVertIndex + surf.numVerts);
	MeshVertexUVs.Resize(surf.startVertIndex + surf.numVerts);

	FVector3* verts = &MeshVertices[surf.startVertIndex];
	FVector2* uvs = &MeshVertexUVs[surf.startVertIndex];

	for (int j = 0; j < surf.numVerts; j++)
	{
		seg_t *seg = &sub->firstline[(surf.numVerts - 1) - j];
		FVector2 v1 = ToFVector2(seg->v1->fPos());

		verts[j].X = v1.X;
		verts[j].Y = v1.Y;
		verts[j].Z = (float)plane.ZatPoint(verts[j]);

		uvs[j] = (mat * FVector4(v1.X / 64.f, -v1.Y / 64.f, 0.f, 1.f)).XY(); // The magic 64.f and negative Y is based on SetFlatVertex
	}

	surf.Type = ST_FLOOR;
	surf.TypeIndex = typeIndex;
	surf.sampleDimension = (controlSector ? controlSector : sector)->planes[sector_t::floor].LightmapSampleDistance;
	surf.ControlSector = controlSector;
	surf.plane = FVector4((float)plane.Normal().X, (float)plane.Normal().Y, (float)plane.Normal().Z, -(float)plane.D);
	surf.sectorGroup = sectorGroup[sector->Index()];

	Surfaces.Push(surf);
}

void DoomLevelSubmesh::CreateCeilingSurface(FLevelLocals& doomMap, subsector_t* sub, sector_t* sector, sector_t* controlSector, int typeIndex)
{
	DoomLevelMeshSurface surf;
	surf.Submesh = this;

	secplane_t plane;
	if (!controlSector)
	{
		plane = sector->ceilingplane;
		surf.bSky = IsSkySector(sector, sector_t::ceiling);
	}
	else
	{
		plane = controlSector->floorplane;
		plane.FlipVert();
		surf.bSky = false;
	}

	surf.numVerts = sub->numlines;
	surf.startVertIndex = MeshVertices.Size();
	surf.texture = (controlSector ? controlSector : sector)->planes[sector_t::ceiling].Texture;

	FGameTexture* txt = TexMan.GetGameTexture(surf.texture);
	float w = txt->GetDisplayWidth();
	float h = txt->GetDisplayHeight();
	VSMatrix mat = GetPlaneTextureRotationMatrix(txt, sector, sector_t::ceiling);

	MeshVertices.Resize(surf.startVertIndex + surf.numVerts);
	MeshVertexUVs.Resize(surf.startVertIndex + surf.numVerts);

	FVector3* verts = &MeshVertices[surf.startVertIndex];
	FVector2* uvs = &MeshVertexUVs[surf.startVertIndex];

	for (int j = 0; j < surf.numVerts; j++)
	{
		seg_t *seg = &sub->firstline[j];
		FVector2 v1 = ToFVector2(seg->v1->fPos());

		verts[j].X = v1.X;
		verts[j].Y = v1.Y;
		verts[j].Z = (float)plane.ZatPoint(verts[j]);

		uvs[j] = (mat * FVector4(v1.X / 64.f, -v1.Y / 64.f, 0.f, 1.f)).XY(); // The magic 64.f and negative Y is based on SetFlatVertex
	}

	surf.Type = ST_CEILING;
	surf.TypeIndex = typeIndex;
	surf.sampleDimension = (controlSector ? controlSector : sector)->planes[sector_t::ceiling].LightmapSampleDistance;
	surf.ControlSector = controlSector;
	surf.plane = FVector4((float)plane.Normal().X, (float)plane.Normal().Y, (float)plane.Normal().Z, -(float)plane.D);
	surf.sectorGroup = sectorGroup[sector->Index()];

	Surfaces.Push(surf);
}

void DoomLevelSubmesh::CreateSubsectorSurfaces(FLevelLocals &doomMap)
{
	for (unsigned int i = 0; i < doomMap.subsectors.Size(); i++)
	{
		subsector_t *sub = &doomMap.subsectors[i];

		if (sub->numlines < 3)
		{
			continue;
		}

		sector_t *sector = sub->sector;
		if (!sector)
			continue;

		CreateFloorSurface(doomMap, sub, sector, nullptr, i);
		CreateCeilingSurface(doomMap, sub, sector, nullptr, i);

		for (unsigned int j = 0; j < sector->e->XFloor.ffloors.Size(); j++)
		{
			CreateFloorSurface(doomMap, sub, sector, sector->e->XFloor.ffloors[j]->model, i);
			CreateCeilingSurface(doomMap, sub, sector, sector->e->XFloor.ffloors[j]->model, i);
		}
	}
}

bool DoomLevelSubmesh::IsTopSideSky(sector_t* frontsector, sector_t* backsector, side_t* side)
{
	return IsSkySector(frontsector, sector_t::ceiling) && IsSkySector(backsector, sector_t::ceiling);
}

bool DoomLevelSubmesh::IsTopSideVisible(side_t* side)
{
	auto tex = TexMan.GetGameTexture(side->GetTexture(side_t::top), true);
	return tex && tex->isValid();
}

bool DoomLevelSubmesh::IsBottomSideVisible(side_t* side)
{
	auto tex = TexMan.GetGameTexture(side->GetTexture(side_t::bottom), true);
	return tex && tex->isValid();
}

bool DoomLevelSubmesh::IsSkySector(sector_t* sector, int plane)
{
	// plane is either sector_t::ceiling or sector_t::floor
	return sector->GetTexture(plane) == skyflatnum;
}

bool DoomLevelSubmesh::IsDegenerate(const FVector3 &v0, const FVector3 &v1, const FVector3 &v2)
{
	// A degenerate triangle has a zero cross product for two of its sides.
	float ax = v1.X - v0.X;
	float ay = v1.Y - v0.Y;
	float az = v1.Z - v0.Z;
	float bx = v2.X - v0.X;
	float by = v2.Y - v0.Y;
	float bz = v2.Z - v0.Z;
	float crossx = ay * bz - az * by;
	float crossy = az * bx - ax * bz;
	float crossz = ax * by - ay * bx;
	float crosslengthsqr = crossx * crossx + crossy * crossy + crossz * crossz;
	return crosslengthsqr <= 1.e-6f;
}

void DoomLevelSubmesh::DumpMesh(const FString& objFilename, const FString& mtlFilename) const
{
	auto f = fopen(objFilename.GetChars(), "w");

	fprintf(f, "# DoomLevelMesh debug export\n");
	fprintf(f, "# MeshVertices: %u, MeshElements: %u, Surfaces: %u\n", MeshVertices.Size(), MeshElements.Size(), Surfaces.Size());
	fprintf(f, "mtllib %s\n", mtlFilename.GetChars());

	double scale = 1 / 10.0;

	for (const auto& v : MeshVertices)
	{
		fprintf(f, "v %f %f %f\n", v.X * scale, v.Y * scale, v.Z * scale);
	}

	{
		for (const auto& uv : LightmapUvs)
		{
			fprintf(f, "vt %f %f\n", uv.X, uv.Y);
		}
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
		case ST_UNKNOWN:
			return "unknown";
		default:
			break;
		}
		return "error";
	};


	uint32_t lastSurfaceIndex = -1;


	bool useErrorMaterial = false;
	int highestUsedAtlasPage = -1;

	for (unsigned i = 0, count = MeshElements.Size(); i + 2 < count; i += 3)
	{
		auto index = MeshSurfaceIndexes[i / 3];

		if(index != lastSurfaceIndex)
		{
			lastSurfaceIndex = index;

			if (unsigned(index) >= Surfaces.Size())
			{
				fprintf(f, "o Surface[%d] (bad index)\n", index);
				fprintf(f, "usemtl error\n");

				useErrorMaterial = true;
			}
			else
			{
				const auto& surface = Surfaces[index];
				fprintf(f, "o Surface[%d] %s %d%s\n", index, name(surface.Type), surface.TypeIndex, surface.bSky ? " sky" : "");
				fprintf(f, "usemtl lightmap%d\n", surface.AtlasTile.ArrayIndex);

				if (surface.AtlasTile.ArrayIndex > highestUsedAtlasPage)
				{
					highestUsedAtlasPage = surface.AtlasTile.ArrayIndex;
				}
			}
		}

		// fprintf(f, "f %d %d %d\n", MeshElements[i] + 1, MeshElements[i + 1] + 1, MeshElements[i + 2] + 1);
		fprintf(f, "f %d/%d %d/%d %d/%d\n",
			MeshElements[i + 0] + 1, MeshElements[i + 0] + 1,
			MeshElements[i + 1] + 1, MeshElements[i + 1] + 1,
			MeshElements[i + 2] + 1, MeshElements[i + 2] + 1);

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

void DoomLevelSubmesh::SetupLightmapUvs(FLevelLocals& doomMap)
{
	LMTextureSize = 1024; // TODO cvar

	for (auto& surface : Surfaces)
	{
		BuildSurfaceParams(LMTextureSize, LMTextureSize, surface);
	}
}

void DoomLevelSubmesh::PackLightmapAtlas(int lightmapStartIndex)
{
	std::vector<LevelMeshSurface*> sortedSurfaces;
	sortedSurfaces.reserve(Surfaces.Size());

	for (auto& surface : Surfaces)
	{
		sortedSurfaces.push_back(&surface);
	}

	std::sort(sortedSurfaces.begin(), sortedSurfaces.end(), [](LevelMeshSurface* a, LevelMeshSurface* b) { return a->AtlasTile.Height != b->AtlasTile.Height ? a->AtlasTile.Height > b->AtlasTile.Height : a->AtlasTile.Width > b->AtlasTile.Width; });

	RectPacker packer(LMTextureSize, LMTextureSize, RectPacker::Spacing(0));

	for (LevelMeshSurface* surf : sortedSurfaces)
	{
		int sampleWidth = surf->AtlasTile.Width;
		int sampleHeight = surf->AtlasTile.Height;

		auto result = packer.insert(sampleWidth, sampleHeight);
		int x = result.pos.x, y = result.pos.y;

		surf->AtlasTile.X = x;
		surf->AtlasTile.Y = y;
		surf->AtlasTile.ArrayIndex = lightmapStartIndex + (int)result.pageIndex;

		// calculate final texture coordinates
		for (int i = 0; i < (int)surf->numVerts; i++)
		{
			auto& u = LightmapUvs[surf->startUvIndex + i].X;
			auto& v = LightmapUvs[surf->startUvIndex + i].Y;
			u = (u + x) / (float)LMTextureSize;
			v = (v + y) / (float)LMTextureSize;
		}
	}

	LMTextureCount = (int)packer.getNumPages();
}

BBox DoomLevelSubmesh::GetBoundsFromSurface(const LevelMeshSurface& surface) const
{
	constexpr float M_INFINITY = 1e30f; // TODO cleanup

	FVector3 low(M_INFINITY, M_INFINITY, M_INFINITY);
	FVector3 hi(-M_INFINITY, -M_INFINITY, -M_INFINITY);

	for (int i = int(surface.startVertIndex); i < int(surface.startVertIndex) + surface.numVerts; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			if (MeshVertices[i][j] < low[j])
			{
				low[j] = MeshVertices[i][j];
			}
			if (MeshVertices[i][j] > hi[j])
			{
				hi[j] = MeshVertices[i][j];
			}
		}
	}

	BBox bounds;
	bounds.Clear();
	bounds.min = low;
	bounds.max = hi;
	return bounds;
}

DoomLevelSubmesh::PlaneAxis DoomLevelSubmesh::BestAxis(const FVector4& p)
{
	float na = fabs(float(p.X));
	float nb = fabs(float(p.Y));
	float nc = fabs(float(p.Z));

	// figure out what axis the plane lies on
	if (na >= nb && na >= nc)
	{
		return AXIS_YZ;
	}
	else if (nb >= na && nb >= nc)
	{
		return AXIS_XZ;
	}

	return AXIS_XY;
}

void DoomLevelSubmesh::BuildSurfaceParams(int lightMapTextureWidth, int lightMapTextureHeight, LevelMeshSurface& surface)
{
	BBox bounds = GetBoundsFromSurface(surface);
	surface.bounds = bounds;

	if (surface.sampleDimension <= 0)
	{
		surface.sampleDimension = LightmapSampleDistance;
	}

	surface.sampleDimension = uint16_t(max(int(roundf(float(surface.sampleDimension) / max(1.0f / 4, float(lm_scale)))), 1));

	{
		// Round to nearest power of two
		uint32_t n = uint16_t(surface.sampleDimension);
		n |= n >> 1;
		n |= n >> 2;
		n |= n >> 4;
		n |= n >> 8;
		n = (n + 1) >> 1;
		surface.sampleDimension = uint16_t(n) ? uint16_t(n) : uint16_t(0xFFFF);
	}

	// round off dimensions
	FVector3 roundedSize;
	for (int i = 0; i < 3; i++)
	{
		bounds.min[i] = surface.sampleDimension * (floor(bounds.min[i] / surface.sampleDimension) - 1);
		bounds.max[i] = surface.sampleDimension * (ceil(bounds.max[i] / surface.sampleDimension) + 1);
		roundedSize[i] = (bounds.max[i] - bounds.min[i]) / surface.sampleDimension;
	}

	FVector3 tCoords[2] = { FVector3(0.0f, 0.0f, 0.0f), FVector3(0.0f, 0.0f, 0.0f) };

	PlaneAxis axis = BestAxis(surface.plane);

	int width;
	int height;
	switch (axis)
	{
	default:
	case AXIS_YZ:
		width = (int)roundedSize.Y;
		height = (int)roundedSize.Z;
		tCoords[0].Y = 1.0f / surface.sampleDimension;
		tCoords[1].Z = 1.0f / surface.sampleDimension;
		break;

	case AXIS_XZ:
		width = (int)roundedSize.X;
		height = (int)roundedSize.Z;
		tCoords[0].X = 1.0f / surface.sampleDimension;
		tCoords[1].Z = 1.0f / surface.sampleDimension;
		break;

	case AXIS_XY:
		width = (int)roundedSize.X;
		height = (int)roundedSize.Y;
		tCoords[0].X = 1.0f / surface.sampleDimension;
		tCoords[1].Y = 1.0f / surface.sampleDimension;
		break;
	}

	// clamp width
	if (width > lightMapTextureWidth - 2)
	{
		tCoords[0] *= ((float)(lightMapTextureWidth - 2) / (float)width);
		width = (lightMapTextureWidth - 2);
	}

	// clamp height
	if (height > lightMapTextureHeight - 2)
	{
		tCoords[1] *= ((float)(lightMapTextureHeight - 2) / (float)height);
		height = (lightMapTextureHeight - 2);
	}

	surface.translateWorldToLocal = bounds.min;
	surface.projLocalToU = tCoords[0];
	surface.projLocalToV = tCoords[1];

	surface.startUvIndex = AllocUvs(surface.numVerts);

	for (int i = 0; i < surface.numVerts; i++)
	{
		FVector3 tDelta = MeshVertices[surface.startVertIndex + i] - surface.translateWorldToLocal;

		LightmapUvs[surface.startUvIndex + i].X = (tDelta | surface.projLocalToU);
		LightmapUvs[surface.startUvIndex + i].Y = (tDelta | surface.projLocalToV);
	}

	// project tCoords so they lie on the plane
	const FVector4& plane = surface.plane;
	float d = ((bounds.min | FVector3(plane.X, plane.Y, plane.Z)) - plane.W) / plane[axis]; //d = (plane->PointToDist(bounds.min)) / plane->Normal()[axis];
	for (int i = 0; i < 2; i++)
	{
		tCoords[i].MakeUnit();
		d = (tCoords[i] | FVector3(plane.X, plane.Y, plane.Z)) / plane[axis]; //d = dot(tCoords[i], plane->Normal()) / plane->Normal()[axis];
		tCoords[i][axis] -= d;
	}

	surface.AtlasTile.Width = width;
	surface.AtlasTile.Height = height;
}
