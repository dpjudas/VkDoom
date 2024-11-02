
#include "templates.h"
#include "doom_levelmesh.h"
#include "g_levellocals.h"
#include "texturemanager.h"
#include "playsim/p_lnspec.h"
#include "c_dispatch.h"
#include "g_levellocals.h"
#include "a_dynlight.h"
#include "hw_renderstate.h"
#include "hw_vertexbuilder.h"
#include "hw_dynlightdata.h"
#include "hwrenderer/scene/hw_lighting.h"
#include "hwrenderer/scene/hw_drawstructs.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hwrenderer/scene/hw_walldispatcher.h"
#include "hwrenderer/scene/hw_flatdispatcher.h"
#include <unordered_map>

#include "vm.h"

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

static int InvalidateLightmap()
{
	int count = 0;

	for (auto& tile : level.levelMesh->Lightmap.Tiles)
	{
		if (!tile.NeedsUpdate)
			++count;
		tile.NeedsUpdate = true;
	}

	return count;
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

	uint32_t atlasPixelCount = levelMesh->AtlasPixelCount();
	auto stats = levelMesh->GatherTilePixelStats();

	out.Format("Surfaces: %u (awaiting updates: %u)\nSurface pixel area to update: %u\nSurface pixel area: %u\nAtlas pixel area:   %u\nAtlas efficiency: %.4f%%",
		stats.tiles.total, stats.tiles.dirty,
		stats.pixels.dirty,
		stats.pixels.total,
		atlasPixelCount,
		float(stats.pixels.total) / float(atlasPixelCount) * 100.0f );

	return out;
}

ADD_STAT(levelmesh)
{
	auto& stats = level.levelMesh->LastFrameStats;
	FString out;
	if (level.levelMesh)
		out.Format("Sides=%d, flats=%d, portals=%d, dynlights=%d", stats.SidesUpdated, stats.FlatsUpdated, stats.Portals, stats.DynLights);
	else
		out = "No level mesh";
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

	int count = InvalidateLightmap();
	for (auto& tile : level.levelMesh->Lightmap.Tiles)
	{
		if (!tile.NeedsUpdate)
			++count;
		tile.NeedsUpdate = true;
	}
	Printf("Marked %d out of %d tiles for update.\n", count, level.levelMesh->Lightmap.Tiles.Size());
}

CCMD(savelightmap)
{
	if (!RequireLightmap()) return;

	level.levelMesh->SaveLightmapLump(level);
}

CCMD(deletelightmap)
{
	if (!RequireLightmap()) return;

	level.levelMesh->DeleteLightmapLump(level);
}

void DoomLevelMesh::PrintSurfaceInfo(const LevelMeshSurface* surface)
{
	if (!RequireLevelMesh()) return;

	int surfaceIndex = (int)(ptrdiff_t)(surface - Mesh.Surfaces.Data());
	const auto& info = DoomSurfaceInfos[surfaceIndex];

	auto gameTexture = surface->Texture;

	Printf("Surface %d (%p)\n    Type: %d, TypeIndex: %d, ControlSector: %d\n", surfaceIndex, surface, info.Type, info.TypeIndex, info.ControlSector ? info.ControlSector->Index() : -1);
	if (surface->LightmapTileIndex >= 0)
	{
		LightmapTile* tile = &Lightmap.Tiles[surface->LightmapTileIndex];
		Printf("    Atlas page: %d, x:%d, y:%d\n", tile->AtlasLocation.ArrayIndex, tile->AtlasLocation.X, tile->AtlasLocation.Y);
		Printf("    Pixels: %dx%d (area: %d)\n", tile->AtlasLocation.Width, tile->AtlasLocation.Height, tile->AtlasLocation.Area());
		Printf("    Sample dimension: %d\n", tile->SampleDimension);
		Printf("    Needs update?: %d\n", tile->NeedsUpdate);
		Printf("    Always update?: %d\n", tile->AlwaysUpdate);
	}
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

	const auto surface = level.levelMesh->Trace(posXYZ, FVector3(RayDir(angle, pitch)), 32000.0f);
	if (surface)
	{
		level.levelMesh->PrintSurfaceInfo(surface);
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
	SetLimits(doomMap);

	SunColor = doomMap.SunColor; // TODO keep only one copy?
	SunDirection = doomMap.SunDirection;
	Lightmap.SampleDistance = doomMap.LightmapSampleDistance;

	// HWWall and HWFlat still looks at r_viewpoint when doing calculations,
	// but we aren't rendering a specific viewpoint when this function gets called
	int oldextralight = r_viewpoint.extralight;
	AActor* oldcamera = r_viewpoint.camera;
	r_viewpoint.extralight = 0;
	r_viewpoint.camera = nullptr;

	BuildSectorGroups(doomMap);
	CreatePortals(doomMap);
	CreateSurfaces(doomMap);

	// This is a bit of a hack. Lights aren't available until BeginFrame is called.
	// Unfortunately we need a surface list already at this point for our Mesh.MaxNodes calculation
	for (unsigned int i = 0; i < Sides.Size(); i++)
		UpdateSide(i, SurfaceUpdateType::Full);
	for (unsigned int i = 0; i < Flats.Size(); i++)
		UpdateFlat(i, SurfaceUpdateType::Full);

	CreateCollision();
	UploadPortals();
	SortDrawLists();

	r_viewpoint.extralight = oldextralight;
	r_viewpoint.camera = oldcamera;
}

void DoomLevelMesh::SetLimits(FLevelLocals& doomMap)
{
	// Try to estimate what the worst case memory needs are for the level
	LevelMeshLimits limits;

	for (const sector_t& sector : doomMap.sectors)
	{
		unsigned int ffloors = sector.e ? sector.e->XFloor.ffloors.Size() : 0;

		limits.MaxVertices += sector.Lines.Size() * 4 * (3 + ffloors);
		limits.MaxIndexes += sector.Lines.Size() * 6 * (3 + ffloors);
		limits.MaxSurfaces += sector.Lines.Size() * (3 + ffloors);
		limits.MaxUniforms += sector.Lines.Size() * (3 + ffloors);

		limits.MaxSurfaces += sector.subsectorcount * (2 + ffloors * 2);
		limits.MaxUniforms += 2 + ffloors * 2;

		for (int i = 0, count = sector.subsectorcount; i < count; i++)
		{
			limits.MaxVertices += sector.subsectors[i]->numlines;
			limits.MaxIndexes += sector.subsectors[i]->numlines * 3;
		}
	}

	// Double up to leave room for fragmentation
	limits.MaxVertices *= 2;
	limits.MaxSurfaces *= 2;
	limits.MaxUniforms *= 2;
	limits.MaxIndexes *= 2;

	Reset(limits);
}

void DoomLevelMesh::BeginFrame(FLevelLocals& doomMap)
{
	LastFrameStats = CurFrameStats;
	CurFrameStats = Stats();

	// HWWall and HWFlat still looks at r_viewpoint when doing calculations,
	// but we aren't rendering a specific viewpoint when this function gets called
	int oldextralight = r_viewpoint.extralight;
	AActor* oldcamera = r_viewpoint.camera;
	r_viewpoint.extralight = 0;
	r_viewpoint.camera = nullptr;

	ClearDynamicLightmapAtlas();

	for (side_t* side : PolySides)
	{
		UpdateSide(side->Index(), SurfaceUpdateType::Full);
	}

	for (int sideIndex : SideUpdateList)
	{
		if (Sides[sideIndex].UpdateType == SurfaceUpdateType::LightsOnly)
		{
			SetSideLights(doomMap, sideIndex);
		}
		else // SurfaceUpdateType::Full
		{
			CreateSide(doomMap, sideIndex);
		}
		Sides[sideIndex].UpdateType = SurfaceUpdateType::None;
	}
	SideUpdateList.Clear();

	for (int flatIndex : FlatUpdateList)
	{
		if (Flats[flatIndex].UpdateType == SurfaceUpdateType::LightsOnly)
		{
			SetFlatLights(doomMap, flatIndex);
		}
		else // SurfaceUpdateType::Full
		{
			CreateFlat(doomMap, flatIndex);
		}
		Flats[flatIndex].UpdateType = SurfaceUpdateType::None;
	}
	FlatUpdateList.Clear();

	PackDynamicLightmapAtlas();
	UpdateWallPortals();
	UploadDynLights(doomMap);

	Collision->Update();

	r_viewpoint.extralight = oldextralight;
	r_viewpoint.camera = oldcamera;
}

void DoomLevelMesh::UploadDynLights(FLevelLocals& doomMap)
{
	lightdata.Clear();
	for (auto light = doomMap.lights; light; light = light->next)
	{
		if (light->Trace())
		{
			UpdateLight(light);
		}
		else
		{
			int portalGroup = 0; // What value should this have?
			AddLightToList(lightdata, portalGroup, light, false);
			CurFrameStats.DynLights++;
		}
	}

	// All meaasurements here are in vec4's.
	int size0 = lightdata.arrays[0].Size() / 4;
	int size1 = lightdata.arrays[1].Size() / 4;
	int size2 = lightdata.arrays[2].Size() / 4;
	int totalsize = size0 + size1 + size2 + 1;
	int maxLightData = Mesh.DynLights.Size();

	// Clamp lights so they aren't bigger than what fits into a single dynamic uniform buffer page
	if (totalsize > maxLightData)
	{
		int diff = totalsize - maxLightData;

		size2 -= diff;
		if (size2 < 0)
		{
			size1 += size2;
			size2 = 0;
		}
		if (size1 < 0)
		{
			size0 += size1;
			size1 = 0;
		}
		totalsize = size0 + size1 + size2 + 1;
	}
	size0 = std::min(size0, maxLightData - 1);

	float parmcnt[] = { 0, float(size0), float(size0 + size1), float(size0 + size1 + size2) };

	float* copyptr = (float*)Mesh.DynLights.Data();
	memcpy(&copyptr[0], parmcnt, sizeof(FVector4));
	memcpy(&copyptr[4], &lightdata.arrays[0][0], size0 * sizeof(FVector4));
	memcpy(&copyptr[4 + 4 * size0], &lightdata.arrays[1][0], size1 * sizeof(FVector4));
	memcpy(&copyptr[4 + 4 * (size0 + size1)], &lightdata.arrays[2][0], size2 * sizeof(FVector4));

	AddRange(UploadRanges.DynLight, { 0, totalsize });
}

void DoomLevelMesh::UpdateWallPortals()
{
	WallPortals.Clear();
	for (int sideIndex : SidePortals)
	{
		for (HWWall& wall : Sides[sideIndex].WallPortals)
		{
			WallPortals.Push(&wall);
		}
	}
}

void DoomLevelMesh::ProcessDecals(HWDrawInfo* di, FRenderState& state)
{
	for (int sideIndex : level.levelMesh->SideDecals)
	{
		const auto& side = Sides[sideIndex];
		if (side.Decals.Size() == 0)
			continue;

		int dynlightindex = -1;
		if (di->Level->HasDynamicLights && !di->isFullbrightScene() && side.Decals[0].texture != nullptr)
		{
			dynlightindex = side.Decals[0].SetupLights(di, state, lightdata, level.sides[sideIndex].lighthead);
		}

		for (const HWDecalCreateInfo& info : side.Decals)
		{
			info.ProcessDecal(di, state, dynlightindex);
		}
	}
}

int DoomLevelMesh::GetLightIndex(FDynamicLight* light, int portalgroup)
{
	int index;
	for (index = 0; index < FDynamicLight::max_levelmesh_entries && light->levelmesh[index].index != 0; index++)
	{
		if (light->levelmesh[index].portalgroup == portalgroup)
			return light->levelmesh[index].index - 1;
	}
	if (index == FDynamicLight::max_levelmesh_entries)
		return 0;


	LightAllocInfo info = AllocLight();
	CopyToMeshLight(light, *info.Light, portalgroup);

	light->levelmesh[index].index = info.Index + 1;
	light->levelmesh[index].portalgroup = portalgroup;
	return info.Index;
}

void DoomLevelMesh::UpdateLight(FDynamicLight* light)
{
	for (int index = 0; index < FDynamicLight::max_levelmesh_entries && light->levelmesh[index].index != 0; index++)
	{
		int lightindex = light->levelmesh[index].index - 1;
		int portalgroup = light->levelmesh[index].portalgroup;
		CopyToMeshLight(light, Mesh.Lights[lightindex], portalgroup);
		AddRange(UploadRanges.Light, { lightindex, lightindex + 1 });
	}
}

void DoomLevelMesh::CopyToMeshLight(FDynamicLight* light, LevelMeshLight& meshlight, int portalgroup)
{
	DVector3 pos = light->PosRelative(portalgroup);

	meshlight.Origin = { (float)pos.X, (float)pos.Y, (float)pos.Z };
	meshlight.RelativeOrigin = meshlight.Origin;
	meshlight.Radius = (float)light->GetRadius();
	meshlight.Intensity = light->target ? (float)light->target->Alpha : 1.0f;
	if (light->IsSpot() && light->pSpotInnerAngle && light->pSpotOuterAngle && light->pPitch && light->target)
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

	meshlight.SoftShadowRadius = light->GetSoftShadowRadius();

	if (light->Sector)
		meshlight.SectorGroup = sectorGroup[light->Sector->Index()];
	else
		meshlight.SectorGroup = 0;
}

bool DoomLevelMesh::TraceSky(const FVector3& start, FVector3 direction, float dist)
{
	FVector3 end = start + direction * dist;
	auto surface = Trace(start, direction, dist);
	return surface && surface->IsSky;
}

void DoomLevelMesh::CreateSurfaces(FLevelLocals& doomMap)
{
	bindings.clear();
	Sides.clear();
	Flats.clear();
	Sides.resize(doomMap.sides.size());
	Flats.resize(doomMap.sectors.Size());

	// Create surface objects for all sides
	for (unsigned int i = 0; i < doomMap.sides.Size(); i++)
	{
		side_t* side = &doomMap.sides[i];
		bool isPolyLine = !!(side->Flags & WALLF_POLYOBJ);
		if (isPolyLine)
		{
			PolySides.Push(side);
			continue;
		}

		CreateSide(doomMap, i);
	}

	// Create surfaces for all flats
	for (unsigned int i = 0; i < doomMap.sectors.Size(); i++)
	{
		sector_t* sector = &doomMap.sectors[i];
		if (sector->subsectorcount == 0 || sector->subsectors[0]->flags & SSECF_POLYORG)
			continue;
		CreateFlat(doomMap, i);
	}
}

void DoomLevelMesh::FreeSide(FLevelLocals& doomMap, unsigned int sideIndex)
{
	if (sideIndex < 0 || sideIndex >= Sides.Size())
		return;

	int surf = Sides[sideIndex].FirstSurface;
	while (surf != -1)
	{
		unsigned int next = DoomSurfaceInfos[surf].NextSurface;
		FreeSurface(surf);
		surf = next;
	}
	Sides[sideIndex].FirstSurface = -1;

	FreeLightList(Sides[sideIndex].Lights.Start, Sides[sideIndex].Lights.Count);
	Sides[sideIndex].Lights.Count = 0;

	for (auto& geo : Sides[sideIndex].Geometries)
		FreeGeometry(geo.VertexStart, geo.VertexCount, geo.IndexStart, geo.IndexCount);
	Sides[sideIndex].Geometries.Clear();

	RemoveFromDrawList(Sides[sideIndex].DrawRanges);
	Sides[sideIndex].DrawRanges.Clear();

	for (auto& uni : Sides[sideIndex].Uniforms)
		FreeUniforms(uni.Start, uni.Count);
	Sides[sideIndex].Uniforms.Clear();

	Sides[sideIndex].WallPortals.Clear();
	Sides[sideIndex].Decals.Clear();
}

void DoomLevelMesh::FreeFlat(FLevelLocals& doomMap, unsigned int sectorIndex)
{
	if (sectorIndex < 0 || sectorIndex >= Flats.Size())
		return;

	int surf = Flats[sectorIndex].FirstSurface;
	while (surf != -1)
	{
		unsigned int next = DoomSurfaceInfos[surf].NextSurface;
		FreeSurface(surf);
		surf = next;
	}
	Flats[sectorIndex].FirstSurface = -1;

	for (auto& list : Flats[sectorIndex].Lights)
		FreeLightList(list.Start, list.Count);
	Flats[sectorIndex].Lights.Clear();

	for (auto& geo : Flats[sectorIndex].Geometries)
		FreeGeometry(geo.VertexStart, geo.VertexCount, geo.IndexStart, geo.IndexCount);
	Flats[sectorIndex].Geometries.Clear();

	RemoveFromDrawList(Flats[sectorIndex].DrawRanges);
	Flats[sectorIndex].DrawRanges.Clear();

	for (auto& uni : Flats[sectorIndex].Uniforms)
		FreeUniforms(uni.Start, uni.Count);
	Flats[sectorIndex].Uniforms.Clear();
}

void DoomLevelMesh::OnFloorHeightChanged(sector_t* sector)
{
	UpdateFlat(sector->Index(), SurfaceUpdateType::Full);
	for (line_t* line : sector->Lines)
	{
		if (line->sidedef[0])
			UpdateSide(line->sidedef[0]->Index(), SurfaceUpdateType::Full);
		if (line->sidedef[1])
			UpdateSide(line->sidedef[1]->Index(), SurfaceUpdateType::Full);
	}
}

void DoomLevelMesh::OnCeilingHeightChanged(sector_t* sector)
{
	UpdateFlat(sector->Index(), SurfaceUpdateType::Full);
	for (line_t* line : sector->Lines)
	{
		if (line->sidedef[0])
			UpdateSide(line->sidedef[0]->Index(), SurfaceUpdateType::Full);
		if (line->sidedef[1])
			UpdateSide(line->sidedef[1]->Index(), SurfaceUpdateType::Full);
	}
}

void DoomLevelMesh::OnMidTex3DHeightChanged(sector_t* sector)
{
	// UpdateFlat(sector->Index(), SurfaceUpdateType::Full);
}

void DoomLevelMesh::OnFloorTextureChanged(sector_t* sector)
{
	UpdateFlat(sector->Index(), SurfaceUpdateType::Full);
}

void DoomLevelMesh::OnCeilingTextureChanged(sector_t* sector)
{
	UpdateFlat(sector->Index(), SurfaceUpdateType::Full);
}

void DoomLevelMesh::OnSectorChangedOther(sector_t* sector)
{
	UpdateFlat(sector->Index(), SurfaceUpdateType::Full);
}

void DoomLevelMesh::OnSideTextureChanged(side_t* side, int section)
{
	UpdateSide(side->Index(), SurfaceUpdateType::Full);
}

void DoomLevelMesh::OnSideDecalsChanged(side_t* side)
{
	UpdateSide(side->Index(), SurfaceUpdateType::Full);
}

void DoomLevelMesh::OnSectorLightChanged(sector_t* sector)
{
	UpdateFlat(sector->Index(), SurfaceUpdateType::LightsOnly);
	for (line_t* line : sector->Lines)
	{
		if (line->sidedef[0] && line->sidedef[0]->sector == sector)
			UpdateSide(line->sidedef[0]->Index(), SurfaceUpdateType::LightsOnly);
		else if (line->sidedef[1] && line->sidedef[1]->sector == sector)
			UpdateSide(line->sidedef[1]->Index(), SurfaceUpdateType::LightsOnly);
	}
}

void DoomLevelMesh::OnSectorLightThinkerCreated(sector_t* sector, DLighting* lightthinker)
{
}

void DoomLevelMesh::OnSectorLightThinkerDestroyed(sector_t* sector, DLighting* lightthinker)
{
}

void DoomLevelMesh::UpdateSide(unsigned int sideIndex, SurfaceUpdateType updateType)
{
	if (Sides[sideIndex].UpdateType == SurfaceUpdateType::None)
	{
		SideUpdateList.Push(sideIndex);
		Sides[sideIndex].UpdateType = updateType;
	}
}

void DoomLevelMesh::UpdateFlat(unsigned int sectorIndex, SurfaceUpdateType updateType)
{
	if (Flats[sectorIndex].UpdateType == SurfaceUpdateType::None)
	{
		FlatUpdateList.Push(sectorIndex);
		Flats[sectorIndex].UpdateType = updateType;
	}
}

LightListAllocInfo DoomLevelMesh::CreateLightList(FLightNode* node, int portalgroup)
{
	int lightcount = 0;
	FLightNode* cur = node;
	while (cur)
	{
		FDynamicLight* light = cur->lightsource;
		if (light && light->Trace() && GetLightIndex(light, portalgroup) >= 0)
		{
			lightcount++;
		}
		cur = cur->nextLight;
	}

	LightListAllocInfo info = AllocLightList(lightcount);
	int i = 0;
	cur = node;
	while (cur)
	{
		FDynamicLight* light = cur->lightsource;
		if (light && light->Trace())
		{
			int lightindex = GetLightIndex(light, portalgroup);
			if (lightindex >= 0)
			{
				info.List[i++] = lightindex;
			}
		}
		cur = cur->nextLight;
	}
	return info;
}

void DoomLevelMesh::CreateSide(FLevelLocals& doomMap, unsigned int sideIndex)
{
	CurFrameStats.SidesUpdated++;

	FreeSide(doomMap, sideIndex);

	side_t* side = &doomMap.sides[sideIndex];

	seg_t* seg = side->segs[0];
	if (!seg)
		return;

	auto& sideBlock = Sides[sideIndex];

	sector_t* front;
	sector_t* back;
	subsector_t* sub;
	if (side->Flags & WALLF_POLYOBJ)
	{
		sub = level.PointInRenderSubsector((side->V1()->fPos() + side->V2()->fPos()) * 0.5);
		if (!sub)
			return;
		front = sub->sector;
		back = nullptr;
		sideBlock.Lights = CreateLightList(sub->section->lighthead, sub->sector->PortalGroup);
	}
	else
	{
		sub = seg->Subsector;
		front = side->sector;
		back = (side->linedef->frontsector == front) ? side->linedef->backsector : side->linedef->frontsector;
		sideBlock.Lights = CreateLightList(side->lighthead, side->sector->PortalGroup);
	}

	HWMeshHelper result;
	HWWallDispatcher disp(&doomMap, &result, getRealLightmode(&doomMap, true));
	HWWall wall;
	wall.sub = sub;
	wall.Process(&disp, state, seg, front, back);

	// Grab the decals generated
	if (result.decals.Size() != 0 && !sideBlock.InSideDecalsList)
	{
		SideDecals.Push(sideIndex);
		sideBlock.InSideDecalsList = true;
	}

	sideBlock.Decals = result.decals;

	// Part 1: solid geometry. This is set up so that there are no transparent parts
	state.SetDepthFunc(DF_LEqual);
	state.ClearDepthBias();
	state.EnableTexture(true);
	state.EnableBrightmap(true);
	state.AlphaFunc(Alpha_GEqual, 0.f);
	CreateWallSurface(side, disp, state, result.list, back ? LevelMeshDrawType::Masked : LevelMeshDrawType::Opaque, true, sideIndex, sideBlock.Lights);

	if (result.portals.Size() != 0 && !sideBlock.InSidePortalsList)
	{
		// Register side having portals
		SidePortals.Push(sideIndex);
		sideBlock.InSidePortalsList = true;
	}

	for (HWWall& portal : result.portals)
	{
		sideBlock.WallPortals.Push(portal);
	}

	CreateWallSurface(side, disp, state, result.portals, LevelMeshDrawType::Portal, false, sideIndex, sideBlock.Lights);

	/*
	// final pass: translucent stuff
	state.AlphaFunc(Alpha_GEqual, gl_mask_sprite_threshold);
	state.SetRenderStyle(STYLE_Translucent);
	CreateWallSurface(side, disp, state, result.translucent, LevelMeshDrawType::Translucent, true, sideIndex);
	state.AlphaFunc(Alpha_GEqual, 0.f);
	state.SetRenderStyle(STYLE_Normal);
	*/
}

void DoomLevelMesh::CreateFlat(FLevelLocals& doomMap, unsigned int sectorIndex)
{
	CurFrameStats.FlatsUpdated++;

	FreeFlat(doomMap, sectorIndex);

	sector_t* sector = &doomMap.sectors[sectorIndex];
	for (FSection& section : doomMap.sections.SectionsForSector(sectorIndex))
	{
		Flats[sectorIndex].Lights.Push(CreateLightList(section.lighthead, section.sector->PortalGroup));
		const auto& lightlist = Flats[sectorIndex].Lights.Last();

		HWFlatMeshHelper result;
		HWFlatDispatcher disp(&doomMap, &result, getRealLightmode(&doomMap, true));

		HWFlat flat;
		flat.section = &section;
		flat.ProcessSector(&disp, state, sector);

		// Part 1: solid geometry. This is set up so that there are no transparent parts
		state.SetDepthFunc(DF_LEqual);
		state.ClearDepthBias();
		state.EnableTexture(true);
		state.EnableBrightmap(true);
		CreateFlatSurface(disp, state, result.list, LevelMeshDrawType::Opaque, false, sectorIndex, lightlist);

		CreateFlatSurface(disp, state, result.portals, LevelMeshDrawType::Portal, false, sectorIndex, lightlist);

		// final pass: translucent stuff
		state.AlphaFunc(Alpha_GEqual, gl_mask_sprite_threshold);
		state.SetRenderStyle(STYLE_Translucent);
		CreateFlatSurface(disp, state, result.translucentborder, LevelMeshDrawType::Translucent, true, sectorIndex, lightlist);
		state.SetDepthMask(false);
		CreateFlatSurface(disp, state, result.translucent, LevelMeshDrawType::Translucent, true, sectorIndex, lightlist);
		state.AlphaFunc(Alpha_GEqual, 0.f);
		state.SetDepthMask(true);
		state.SetRenderStyle(STYLE_Normal);
	}
}

void DoomLevelMesh::SetSideLights(FLevelLocals& doomMap, unsigned int sideIndex)
{
	ELightMode lightmode = getRealLightmode(&doomMap, true);

	side_t* side = &doomMap.sides[sideIndex];

	// Global modifiers that affects EVERYTHING.
	// We need to find a way to apply this in the shader.
	int rel = getExtraLight();
	bool fullbrightScene = isFullbrightScene();

	// To do: we need to know where each uniform block came from:
	sector_t* frontsector = side->sector;
	FColormap Colormap = frontsector->Colormap; // To do: this may come from the lightlist
	bool foggy = (!Colormap.FadeColor.isBlack() || doomMap.flags & LEVEL_HASFADETABLE);	// fog disables fake contrast
	float alpha = 1.0f;
	if (side->linedef->alpha != 0)
	{
		switch (side->linedef->flags & ML_ADDTRANS)
		{
		case 0:
		case ML_ADDTRANS:
			alpha = side->linedef->alpha;
		}
	}
	float absalpha = fabsf(alpha);

	// GetLightLevel changes global extra light. Used for the fake contrast:
	int orglightlevel = hw_ClampLight(frontsector->lightlevel);
	int lightlevel = hw_ClampLight(side->GetLightLevel(foggy, orglightlevel, side_t::mid, false, &rel));

	for (UniformsAllocInfo& uinfo : Sides[sideIndex].Uniforms)
	{
		for (int i = 0, count = uinfo.Count; i < count; i++)
		{
			// To do: calculate this correctly (see HWDrawInfo::SetColor)
			// uinfo.LightUniforms[i].uVertexColor
			// uinfo.LightUniforms[i].uDesaturationFactor
			if (uinfo.LightUniforms[i].uLightLevel >= 0.0f) 
			{
				uinfo.LightUniforms[i].uLightLevel = clamp(doomMap.sides[sideIndex].sector->lightlevel * (1.0f / 255.0f), 0.0f, 1.0f);
			}

			SetColor(uinfo.LightUniforms[i], &doomMap, lightmode, lightlevel, rel, fullbrightScene, Colormap, absalpha);
		}
		AddRange(UploadRanges.LightUniforms, { uinfo.Start, uinfo.Start + uinfo.Count });
	}
}

void DoomLevelMesh::SetFlatLights(FLevelLocals& doomMap, unsigned int sectorIndex)
{
	ELightMode lightmode = getRealLightmode(&doomMap, true);

	// Global modifiers that affects EVERYTHING.
	// We need to find a way to apply this in the shader.
	int rel = 0;// getExtraLight();
	bool fullbrightScene = false; // isFullbrightScene();

	// To do: we need to know where each uniform block came from:
	sector_t* frontsector = &doomMap.sectors[sectorIndex];
	int lightlevel = hw_ClampLight(frontsector->GetFloorLight());
	FColormap Colormap = frontsector->Colormap;
	FSectorPortal* portal = frontsector->ValidatePortal(sector_t::floor);
	double alpha = portal ? frontsector->GetAlpha(sector_t::floor) : 1.0f - frontsector->GetReflect(sector_t::floor);

	for (UniformsAllocInfo& uinfo : Flats[sectorIndex].Uniforms)
	{
		for (int i = 0, count = uinfo.Count; i < count; i++)
		{
			SetColor(uinfo.LightUniforms[i], &doomMap, lightmode, lightlevel, rel, fullbrightScene, Colormap, alpha);
		}
		AddRange(UploadRanges.LightUniforms, { uinfo.Start, uinfo.Start + uinfo.Count });
	}
}

void DoomLevelMesh::CreateWallSurface(side_t* side, HWWallDispatcher& disp, MeshBuilder& state, TArray<HWWall>& list, LevelMeshDrawType drawType, bool translucent, unsigned int sideIndex, const LightListAllocInfo& lightlist)
{
	for (HWWall& wallpart : list)
	{
		state.mSortedLists.clear();
		state.mVertices.Clear();
		state.mIndexes.Clear();

		if (drawType == LevelMeshDrawType::Portal)
		{
			state.SetEffect(EFF_PORTAL);
			state.EnableTexture(false);
			state.SetRenderStyle(STYLE_Normal);

			wallpart.MakeVertices(state, false);
			wallpart.RenderWall(state, HWWall::RWF_BLANK);
			wallpart.vertcount = 0;

			wallpart.LevelMeshInfo.Type = ST_NONE;
			wallpart.LevelMeshInfo.ControlSector = nullptr;

			state.SetEffect(EFF_NONE);
			state.EnableTexture(true);
		}
		else
		{
			if (wallpart.texture && wallpart.texture->isMasked())
			{
				state.AlphaFunc(Alpha_GEqual, gl_mask_threshold);
			}
			else
			{
				state.AlphaFunc(Alpha_GEqual, 0.f);
			}

			wallpart.DrawWall(&disp, state, translucent);
		}

		int numVertices = 0;
		int numIndexes = 0;
		int numUniforms = 0;
		for (auto& it : state.mSortedLists)
		{
			numUniforms++;
			for (MeshDrawCommand& command : it.second.mDraws)
			{
				if (command.DrawType == DT_TriangleFan && command.Count >= 3)
				{
					numVertices += command.Count;
					numIndexes += (command.Count - 2) * 3;
				}
			}
		}

		GeometryAllocInfo ginfo = AllocGeometry(numVertices, numIndexes);
		UniformsAllocInfo uinfo = AllocUniforms(numUniforms);
		SurfaceAllocInfo sinfo = AllocSurface();

		SurfaceUniforms* curUniforms = uinfo.Uniforms;
		SurfaceLightUniforms* curLightUniforms = uinfo.LightUniforms;
		FMaterialState* curMaterial = uinfo.Materials;

		int pipelineID = 0;
		int uniformsIndex = uinfo.Start;
		int vertIndex = ginfo.VertexStart;
		for (auto& it : state.mSortedLists)
		{
			const MeshApplyState& applyState = it.first;

			pipelineID = screen->GetLevelMeshPipelineID(applyState.applyData, applyState.surfaceUniforms, applyState.material);

			for (MeshDrawCommand& command : it.second.mDraws)
			{
				if (command.DrawType == DT_TriangleFan && command.Count >= 3)
				{
					for (int i = 2, count = command.Count; i < count; i++)
					{
						*(ginfo.Indexes++) = vertIndex;
						*(ginfo.Indexes++) = vertIndex + i - 1;
						*(ginfo.Indexes++) = vertIndex + i;
					}

					for (int i = command.Start, end = command.Start + command.Count; i < end; i++)
					{
						*(ginfo.Vertices++) = state.mVertices[i];
						*(ginfo.UniformIndexes++) = uniformsIndex;
					}
					vertIndex += command.Count;
				}
			}

			*(curUniforms++) = applyState.surfaceUniforms;
			*(curMaterial++) = applyState.material;

			curLightUniforms->uVertexColor = applyState.surfaceUniforms.uVertexColor;
			curLightUniforms->uDesaturationFactor = applyState.surfaceUniforms.uDesaturationFactor;
			curLightUniforms->uLightLevel = applyState.surfaceUniforms.uLightLevel;
			curLightUniforms++;

			uniformsIndex++;
		}

		FVector2 v1 = FVector2(side->V1()->fPos());
		FVector2 v2 = FVector2(side->V2()->fPos());
		FVector2 N = FVector2(v2.Y - v1.Y, v1.X - v2.X).Unit();

		uint16_t sampleDimension = 0;
		if (wallpart.LevelMeshInfo.Type == ST_UPPERSIDE)
		{
			sampleDimension = side->textures[side_t::top].LightmapSampleDistance;
		}
		else if (wallpart.LevelMeshInfo.Type == ST_MIDDLESIDE)
		{
			sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;
		}
		else if (wallpart.LevelMeshInfo.Type == ST_LOWERSIDE)
		{
			sampleDimension = side->textures[side_t::bottom].LightmapSampleDistance;
		}

		DoomSurfaceInfo& info = DoomSurfaceInfos[sinfo.Index];
		info.Type = wallpart.LevelMeshInfo.Type;
		info.ControlSector = wallpart.LevelMeshInfo.ControlSector;
		info.TypeIndex = side->Index();
		info.Side = side;

		info.NextSurface = Sides[sideIndex].FirstSurface;
		Sides[sideIndex].FirstSurface = sinfo.Index;

		sinfo.Surface->PipelineID = pipelineID;
		sinfo.Surface->SectorGroup = sectorGroup[side->sector->Index()];
		sinfo.Surface->Alpha = float(side->linedef->alpha);
		sinfo.Surface->MeshLocation.StartVertIndex = ginfo.VertexStart;
		sinfo.Surface->MeshLocation.StartElementIndex = ginfo.IndexStart;
		sinfo.Surface->MeshLocation.NumVerts = ginfo.VertexCount;
		sinfo.Surface->MeshLocation.NumElements = ginfo.IndexCount;
		sinfo.Surface->Plane = FVector4(N.X, N.Y, 0.0f, v1 | N);
		sinfo.Surface->Texture = wallpart.texture;
		sinfo.Surface->PortalIndex = (drawType == LevelMeshDrawType::Portal) ? linePortals[side->linedef->Index()] : 0;
		sinfo.Surface->IsSky = (drawType == LevelMeshDrawType::Portal) ? (wallpart.portaltype == PORTALTYPE_SKY || wallpart.portaltype == PORTALTYPE_SKYBOX || wallpart.portaltype == PORTALTYPE_HORIZON) : false;
		sinfo.Surface->Bounds = GetBoundsFromSurface(*sinfo.Surface);
		sinfo.Surface->LightList.Pos = lightlist.Start;
		sinfo.Surface->LightList.Count = lightlist.Count;

		if (side->Flags & WALLF_POLYOBJ) // Polyobjects gets a new tile every frame
		{
			LightmapTileBinding binding;
			binding.Type = info.Type;
			binding.TypeIndex = info.TypeIndex;
			binding.ControlSector = info.ControlSector ? info.ControlSector->Index() : (int)0xffffffffUL;

			LightmapTile tile;
			tile.Binding = binding;
			tile.Bounds = sinfo.Surface->Bounds;
			tile.Plane = sinfo.Surface->Plane;
			tile.SampleDimension = GetSampleDimension(sampleDimension);
			tile.AlwaysUpdate = 2; // Ignore lm_dynamic

			sinfo.Surface->LightmapTileIndex = Lightmap.Tiles.Size();
			Lightmap.Tiles.Push(tile);
			Lightmap.DynamicSurfaces.Push(sinfo.Index);
		}
		else
		{
			sinfo.Surface->LightmapTileIndex = disp.Level->lightmaps ? AddSurfaceToTile(info, *sinfo.Surface, sampleDimension, !!(side->sector->Flags & SECF_LM_DYNAMIC)) : -1;
		}
		
		SetSideLightmap(sinfo.Index);

		for (int i = ginfo.IndexStart / 3, end = (ginfo.IndexStart + ginfo.IndexCount) / 3; i < end; i++)
			Mesh.SurfaceIndexes[i] = sinfo.Index;

		Sides[sideIndex].Geometries.Push(ginfo);
		Sides[sideIndex].Uniforms.Push(uinfo);

		AddToDrawList(Sides[sideIndex].DrawRanges, pipelineID, ginfo.IndexStart, ginfo.IndexCount, drawType);
	}
}

void DoomLevelMesh::AddToDrawList(TArray<DrawRangeInfo>& drawRanges, int pipelineID, int indexStart, int indexCount, LevelMeshDrawType drawType)
{
	// Remember the location if we have to remove it again
	DrawRangeInfo info;
	info.DrawIndexStart = RemoveRange(FreeLists.DrawIndex, indexCount);
	info.DrawIndexCount = indexCount;
	info.DrawType = drawType;
	info.PipelineID = pipelineID;
	drawRanges.Push(info);

	// Copy the indexes over from the unsorted index list
	memcpy(&Mesh.DrawIndexes[info.DrawIndexStart], &Mesh.Indexes[indexStart], indexCount * sizeof(uint32_t));
	AddRange(UploadRanges.DrawIndex, { info.DrawIndexStart, info.DrawIndexStart + indexCount });

	// Add to the draw lists
	AddRange(DrawList[(int)drawType][pipelineID], { info.DrawIndexStart, info.DrawIndexStart + indexCount });
}

void DoomLevelMesh::RemoveFromDrawList(const TArray<DrawRangeInfo>& drawRanges)
{
	for (const DrawRangeInfo& info : drawRanges)
	{
		int start = info.DrawIndexStart;
		int end = info.DrawIndexStart + info.DrawIndexCount;

		RemoveRange(DrawList[(int)info.DrawType][info.PipelineID], { start, end });
		AddRange(FreeLists.DrawIndex, { start, end });
	}
}

void DoomLevelMesh::SortDrawLists()
{
	std::unordered_map<int, TArray<DrawRangeInfo*>> sortedDrawList[(int)LevelMeshDrawType::NumDrawTypes];

	for (auto& side : Sides)
	{
		for (auto& range : side.DrawRanges)
		{
			sortedDrawList[(int)range.DrawType][range.PipelineID].Push(&range);
		}
	}

	for (auto& flat : Flats)
	{
		for (auto& range : flat.DrawRanges)
		{
			sortedDrawList[(int)range.DrawType][range.PipelineID].Push(&range);
		}
	}

	TArray<uint32_t> indexes;

	for (int drawType = 0; drawType < (int)LevelMeshDrawType::NumDrawTypes; drawType++)
	{
		DrawList[drawType].clear();
		for (auto& it : sortedDrawList[drawType])
		{
			auto& list = DrawList[drawType][it.first];
			int listStart = indexes.Size();
			for (DrawRangeInfo* range : it.second)
			{
				int sortedStart = indexes.Size();
				int start = range->DrawIndexStart;
				int count = range->DrawIndexCount;
				for (int i = 0; i < count; i++)
				{
					indexes.Push(Mesh.DrawIndexes[start + i]);
				}
				range->DrawIndexStart = sortedStart;
			}
			int listEnd = indexes.Size();
			list.Push({ listStart, listEnd });
		}
	}

	memcpy(Mesh.DrawIndexes.Data(), indexes.Data(), indexes.Size() * sizeof(uint32_t));
}

int DoomLevelMesh::AddSurfaceToTile(const DoomSurfaceInfo& info, const LevelMeshSurface& surf, uint16_t sampleDimension, uint8_t alwaysUpdate)
{
	if (surf.IsSky)
		return -1;

	LightmapTileBinding binding;
	binding.Type = info.Type;
	binding.TypeIndex = info.TypeIndex;
	binding.ControlSector = info.ControlSector ? info.ControlSector->Index() : (int)0xffffffffUL;

	auto it = bindings.find(binding);
	if (it != bindings.end())
	{
		int index = it->second;

		if (!Lightmap.StaticAtlasPacked)
		{
			LightmapTile& tile = Lightmap.Tiles[index];
			tile.Bounds.min.X = std::min(tile.Bounds.min.X, surf.Bounds.min.X);
			tile.Bounds.min.Y = std::min(tile.Bounds.min.Y, surf.Bounds.min.Y);
			tile.Bounds.min.Z = std::min(tile.Bounds.min.Z, surf.Bounds.min.Z);
			tile.Bounds.max.X = std::max(tile.Bounds.max.X, surf.Bounds.max.X);
			tile.Bounds.max.Y = std::max(tile.Bounds.max.Y, surf.Bounds.max.Y);
			tile.Bounds.max.Z = std::max(tile.Bounds.max.Z, surf.Bounds.max.Z);
			tile.AlwaysUpdate = max<uint8_t>(tile.AlwaysUpdate, alwaysUpdate);
		}

		return index;
	}
	else
	{
		if (Lightmap.StaticAtlasPacked)
			return -1;

		int index = Lightmap.Tiles.Size();

		LightmapTile tile;
		tile.Binding = binding;
		tile.Bounds = surf.Bounds;
		tile.Plane = surf.Plane;
		tile.SampleDimension = GetSampleDimension(sampleDimension);
		tile.AlwaysUpdate = alwaysUpdate;

		Lightmap.Tiles.Push(tile);
		bindings[binding] = index;
		return index;
	}
}

int DoomLevelMesh::GetSampleDimension(uint16_t sampleDimension)
{
	if (sampleDimension <= 0)
	{
		sampleDimension = Lightmap.SampleDistance;
	}

	sampleDimension = uint16_t(max(int(roundf(float(sampleDimension) / max(1.0f / 4, float(lm_scale)))), 1));

	// Round to nearest power of two
	uint32_t n = uint16_t(sampleDimension);
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n = (n + 1) >> 1;
	sampleDimension = uint16_t(n) ? uint16_t(n) : uint16_t(0xFFFF);

	return sampleDimension;
}

void DoomLevelMesh::CreateFlatSurface(HWFlatDispatcher& disp, MeshBuilder& state, TArray<HWFlat>& list, LevelMeshDrawType drawType, bool translucent, unsigned int sectorIndex, const LightListAllocInfo& lightlist)
{
	for (HWFlat& flatpart : list)
	{
		state.mSortedLists.clear();
		state.mVertices.Clear();
		state.mIndexes.Clear();

		if (drawType == LevelMeshDrawType::Portal)
		{
			state.SetEffect(EFF_PORTAL);
			state.EnableTexture(false);
			state.SetRenderStyle(STYLE_Normal);

			flatpart.DrawSubsectors(&disp, state);

			state.SetEffect(EFF_NONE);
			state.EnableTexture(true);
		}
		else
		{
			if (flatpart.texture && flatpart.texture->isMasked())
			{
				state.AlphaFunc(Alpha_GEqual, gl_mask_threshold);
			}
			else
			{
				state.AlphaFunc(Alpha_GEqual, 0.f);
			}

			flatpart.DrawFlat(&disp, state, translucent);
		}

		VSMatrix textureMatrix;
		textureMatrix.loadIdentity();

		int pipelineID = 0;
		const SurfaceUniforms* uniforms = nullptr;
		const FMaterialState* material = nullptr;
		bool foundDraw = false;
		for (auto& it : state.mSortedLists)
		{
			const MeshApplyState& applyState = it.first;

			pipelineID = screen->GetLevelMeshPipelineID(applyState.applyData, applyState.surfaceUniforms, applyState.material);
			textureMatrix = applyState.textureMatrix;
			uniforms = &applyState.surfaceUniforms;
			material = &applyState.material;
			foundDraw = true;
			break;
		}

		if (!foundDraw)
			continue;

		int numVertices = 0;
		int numIndexes = 0;
		for (subsector_t* sub : flatpart.section->subsectors)
		{
			if (sub->numlines < 3)
				continue;
			numVertices += sub->numlines;
			numIndexes += (sub->numlines - 2) * 3;
		}

		GeometryAllocInfo ginfo = AllocGeometry(numVertices, numIndexes);
		UniformsAllocInfo uinfo = AllocUniforms(1);

		Flats[sectorIndex].Geometries.Push(ginfo);
		Flats[sectorIndex].Uniforms.Push(uinfo);

		int* surfaceIndexes = &Mesh.SurfaceIndexes[ginfo.IndexStart / 3];

		*uinfo.Uniforms = *uniforms;
		*uinfo.Materials = *material;

		uinfo.LightUniforms->uVertexColor = uniforms->uVertexColor;
		uinfo.LightUniforms->uDesaturationFactor = uniforms->uDesaturationFactor;
		uinfo.LightUniforms->uLightLevel = uniforms->uLightLevel;

		int uniformsIndex = uinfo.Start;
		int vertIndex = ginfo.VertexStart;
		int elementIndex = ginfo.IndexStart;

		uint16_t sampleDimension = 0;
		if (flatpart.ceiling)
		{
			sampleDimension = flatpart.sector->planes[sector_t::ceiling].LightmapSampleDistance;
		}
		else
		{
			sampleDimension = flatpart.sector->planes[sector_t::floor].LightmapSampleDistance;
		}

		DoomSurfaceInfo info;
		info.Type = flatpart.ceiling ? ST_CEILING : ST_FLOOR;
		info.ControlSector = flatpart.controlsector ? flatpart.controlsector->model : nullptr;

		LevelMeshSurface surf;
		surf.SectorGroup = sectorGroup[flatpart.sector->Index()];
		surf.Alpha = flatpart.alpha;
		surf.Texture = flatpart.texture;
		surf.PipelineID = pipelineID;
		surf.PortalIndex = sectorPortals[flatpart.ceiling][flatpart.sector->Index()];
		surf.IsSky = (drawType == LevelMeshDrawType::Portal);

		auto plane = info.ControlSector ? info.ControlSector->GetSecPlane(!flatpart.ceiling) : flatpart.sector->GetSecPlane(flatpart.ceiling);
		surf.Plane = FVector4((float)plane.Normal().X, (float)plane.Normal().Y, (float)plane.Normal().Z, -(float)plane.D);

		if (info.ControlSector)
			surf.Plane = -surf.Plane;

		float skyZ = flatpart.ceiling ? 32768.0f : -32768.0f;

		for (subsector_t* sub : flatpart.section->subsectors)
		{
			if (sub->numlines < 3)
				continue;

			int startVertIndex = vertIndex;
			int startElementIndex = elementIndex;
			vertIndex += sub->numlines;
			elementIndex += (sub->numlines - 2) * 3;

			for (int i = 0, end = sub->numlines; i < end; i++)
			{
				auto& vt = sub->firstline[end - 1 - i].v1;

				FVector3 pt((float)vt->fX(), (float)vt->fY(), (drawType == LevelMeshDrawType::Portal) ? skyZ : (float)plane.ZatPoint(vt));
				FVector4 uv = textureMatrix * FVector4(pt.X * (1.0f / 64.0f), pt.Y * (-1.0f / 64.0f), 0.0f, 1.0f);

				FFlatVertex ffv;
				ffv.x = pt.X;
				ffv.y = pt.Y;
				ffv.z = pt.Z;
				ffv.u = uv.X;
				ffv.v = uv.Y;
				ffv.lu = 0.0f;
				ffv.lv = 0.0f;
				ffv.lindex = -1.0f;

				*(ginfo.Vertices++) = ffv;
				*(ginfo.UniformIndexes++) = uniformsIndex;
			}

			SurfaceAllocInfo sinfo = AllocSurface();

			if (flatpart.ceiling)
			{
				for (int i = 2, count = sub->numlines; i < count; i++)
				{
					*(ginfo.Indexes++) = startVertIndex;
					*(ginfo.Indexes++) = startVertIndex + i - 1;
					*(ginfo.Indexes++) = startVertIndex + i;
					*(surfaceIndexes++) = sinfo.Index;
				}
			}
			else
			{
				for (int i = 2, count = sub->numlines; i < count; i++)
				{
					*(ginfo.Indexes++) = startVertIndex + i;
					*(ginfo.Indexes++) = startVertIndex + i - 1;
					*(ginfo.Indexes++) = startVertIndex;
					*(surfaceIndexes++) = sinfo.Index;
				}
			}

			info.TypeIndex = sub->Index();
			info.Subsector = sub;
			surf.MeshLocation.StartVertIndex = startVertIndex;
			surf.MeshLocation.StartElementIndex = startElementIndex;
			surf.MeshLocation.NumVerts = sub->numlines;
			surf.MeshLocation.NumElements = (sub->numlines - 2) * 3;
			surf.Bounds = GetBoundsFromSurface(surf);
			surf.LightmapTileIndex = disp.Level->lightmaps ? AddSurfaceToTile(info, surf, sampleDimension, !!(flatpart.sector->Flags & SECF_LM_DYNAMIC)) : -1;
			surf.LightList.Pos = lightlist.Start;
			surf.LightList.Count = lightlist.Count;

			info.NextSurface = Flats[sectorIndex].FirstSurface;
			Flats[sectorIndex].FirstSurface = sinfo.Index;

			*sinfo.Surface = surf;
			DoomSurfaceInfos[sinfo.Index] = info;

			for (int i = ginfo.IndexStart / 3, end = (ginfo.IndexStart + ginfo.IndexCount) / 3; i < end; i++)
				Mesh.SurfaceIndexes[i] = sinfo.Index;

			SetSubsectorLightmap(sinfo.Index);
		}

		AddToDrawList(Flats[sectorIndex].DrawRanges, pipelineID, ginfo.IndexStart, ginfo.IndexCount, drawType);
	}
}

void DoomLevelMesh::SetSubsectorLightmap(int surfaceIndex)
{
	int lightmapTileIndex = Mesh.Surfaces[surfaceIndex].LightmapTileIndex;
	auto surface = &DoomSurfaceInfos[surfaceIndex];

	if (surface->Subsector->firstline && surface->Subsector->firstline->sidedef)
		surface->Subsector->firstline->sidedef->sector->HasLightmaps = true;

	if (!surface->ControlSector)
	{
		int index = surface->Type == ST_CEILING ? 1 : 0;
		surface->Subsector->LightmapTiles[index][0] = lightmapTileIndex;
	}
	else
	{
		int index = surface->Type == ST_CEILING ? 0 : 1;
		const auto& ffloors = surface->Subsector->sector->e->XFloor.ffloors;
		for (unsigned int i = 0; i < ffloors.Size(); i++)
		{
			if (ffloors[i]->model == surface->ControlSector)
			{
				surface->Subsector->LightmapTiles[index][i + 1] = lightmapTileIndex;
			}
		}
	}
}

void DoomLevelMesh::SetSideLightmap(int surfaceIndex)
{
	int lightmapTileIndex = Mesh.Surfaces[surfaceIndex].LightmapTileIndex;
	auto surface = &DoomSurfaceInfos[surfaceIndex];
	if (!surface->ControlSector)
	{
		if (surface->Type == ST_UPPERSIDE)
		{
			surface->Side->LightmapTiles[0] = lightmapTileIndex;
		}
		else if (surface->Type == ST_MIDDLESIDE)
		{
			surface->Side->LightmapTiles[1] = lightmapTileIndex;
			surface->Side->LightmapTiles[2] = lightmapTileIndex;
		}
		else if (surface->Type == ST_LOWERSIDE)
		{
			surface->Side->LightmapTiles[3] = lightmapTileIndex;
		}
	}
	else
	{
		side_t* backside = surface->Side->linedef->sidedef[surface->Side == surface->Side->linedef->sidedef[0]];
		const auto& ffloors = backside->sector->e->XFloor.ffloors;
		for (unsigned int i = 0; i < ffloors.Size(); i++)
		{
			if (ffloors[i]->model == surface->ControlSector)
			{
				backside->LightmapTiles[4 + i] = lightmapTileIndex;
			}
		}
	}
}

BBox DoomLevelMesh::GetBoundsFromSurface(const LevelMeshSurface& surface) const
{
	BBox bounds;
	bounds.Clear();
	for (int i = int(surface.MeshLocation.StartVertIndex); i < int(surface.MeshLocation.StartVertIndex) + surface.MeshLocation.NumVerts; i++)
	{
		FVector3 v = Mesh.Vertices[(int)i].fPos();
		bounds.min.X = std::min(bounds.min.X, v.X);
		bounds.min.Y = std::min(bounds.min.Y, v.Y);
		bounds.min.Z = std::min(bounds.min.Z, v.Z);
		bounds.max.X = std::max(bounds.max.X, v.X);
		bounds.max.Y = std::max(bounds.max.Y, v.Y);
		bounds.max.Z = std::max(bounds.max.Z, v.Z);
	}
	return bounds;
}

void DoomLevelMesh::GetVisibleSurfaces(LightmapTile* tile, TArray<int>& outSurfaces)
{
	if (tile->Binding.Type == ST_MIDDLESIDE || tile->Binding.Type == ST_UPPERSIDE || tile->Binding.Type == ST_LOWERSIDE)
	{
		int sideIndex = tile->Binding.TypeIndex;
		int surf = Sides[sideIndex].FirstSurface;
		while (surf != -1)
		{
			const auto& sinfo = DoomSurfaceInfos[surf];
			if (sinfo.Type == tile->Binding.Type)
			{
				outSurfaces.Push(surf);
			}
			surf = sinfo.NextSurface;
		}
	}
	else if (tile->Binding.Type == ST_CEILING || tile->Binding.Type == ST_FLOOR)
	{
		int subsectorIndex = tile->Binding.TypeIndex;
		int sectorIndex = level.subsectors[subsectorIndex].sector->Index();
		int surf = Flats[sectorIndex].FirstSurface;
		while (surf != -1)
		{
			const auto& sinfo = DoomSurfaceInfos[surf];
			int controlSector = sinfo.ControlSector ? sinfo.ControlSector->Index() : (int)0xffffffffUL;
			if (sinfo.Type == tile->Binding.Type && controlSector == tile->Binding.ControlSector)
			{
				FVector2 minUV = tile->ToUV(Mesh.Surfaces[surf].Bounds.min);
				FVector2 maxUV = tile->ToUV(Mesh.Surfaces[surf].Bounds.max);
				if (!(maxUV.X < 0.0f || maxUV.Y < 0.0f || minUV.X > 1.0f || minUV.Y > 1.0f))
				{
					outSurfaces.Push(surf);
				}
			}
			surf = sinfo.NextSurface;
		}
	}
}

void DoomLevelMesh::DumpMesh(const FString& objFilename, const FString& mtlFilename) const
{
	auto f = fopen(objFilename.GetChars(), "w");

	// To do: this dumps the entire vertex buffer, including those not in use

	fprintf(f, "# DoomLevelMesh debug export\n");
	fprintf(f, "# Vertices: %u, Indexes: %u, Surfaces: %u\n", Mesh.Vertices.Size(), Mesh.IndexCount, Mesh.Surfaces.Size());
	fprintf(f, "mtllib %s\n", mtlFilename.GetChars());

	double scale = 1 / 10.0;

	for (const auto& v : Mesh.Vertices)
	{
		fprintf(f, "v %f %f %f\n", v.x * scale, v.y * scale, v.z * scale);
	}

	for (const auto& v : Mesh.Vertices)
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

	for (unsigned i = 0, count = Mesh.IndexCount; i + 2 < count; i += 3)
	{
		auto index = Mesh.SurfaceIndexes[i / 3];

		if (index != lastSurfaceIndex)
		{
			lastSurfaceIndex = index;

			if (unsigned(index) >= Mesh.Surfaces.Size())
			{
				fprintf(f, "o Surface[%d] (bad index)\n", index);
				fprintf(f, "usemtl error\n");

				useErrorMaterial = true;
			}
			else
			{
				const auto& info = DoomSurfaceInfos[index];
				const auto& surface = Mesh.Surfaces[index];
				fprintf(f, "o Surface[%d] %s %d%s\n", index, name(info.Type), info.TypeIndex, surface.IsSky ? " sky" : "");

				if (surface.LightmapTileIndex >= 0)
				{
					auto& tile = Lightmap.Tiles[surface.LightmapTileIndex];
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
			Mesh.Indexes[i + 0] + 1, Mesh.Indexes[i + 0] + 1,
			Mesh.Indexes[i + 1] + 1, Mesh.Indexes[i + 1] + 1,
			Mesh.Indexes[i + 2] + 1, Mesh.Indexes[i + 2] + 1);

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
				// Note: Y and Z is swapped in the shader due to how the hwrenderer was implemented
				VSMatrix transformation;
				transformation.loadIdentity();
				transformation.translate((float)d.X, 0.0f, (float)d.Y);

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

			// Note: Y and Z is swapped in the shader due to how the hwrenderer was implemented
			transformation.rotate((float)sourceLine->getPortalAngleDiff().Degrees(), 0.0f, 1.0f, 0.0f);
			transformation.translate((float)(targetXYZ.X - sourceXYZ.X), (float)z, (float)(targetXYZ.Y - sourceXYZ.Y));

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

class LumpWriter
{
public:
	LumpWriter(size_t size) { buffer.Reserve(size); }

	void Write(const void* data, size_t size)
	{
		if (pos + size > buffer.size() || size > 0xffffffff)
			I_FatalError("LumpWriter ran out of space!");
		memcpy(buffer.data() + pos, data, size);
		pos += size;
	}

	void Write8(const uint8_t val) { Write(&val, sizeof(uint8_t)); }
	void Write16(const short val) { Write(&val, sizeof(uint16_t)); }
	void Write32(const int val) { Write(&val, sizeof(uint32_t)); }
	void WriteFloat(const float val) { Write(&val, sizeof(float)); }

	TArray<uint8_t> DeflateCompress()
	{
		TArray<uint8_t> output;

		enum { BUFFER_SIZE = 8192 };
		uint8_t Buffer[BUFFER_SIZE];

		mz_stream Stream = {};
		int err = deflateInit(&Stream, 9);
		if (err != Z_OK)
			I_FatalError("Could not initialize deflate buffer.");

		Stream.next_out = Buffer;
		Stream.avail_out = BUFFER_SIZE;
		Stream.next_in = buffer.data();
		Stream.avail_in = buffer.size();
		err = mz_deflate(&Stream, 0);
		while (Stream.avail_out == 0 && err == Z_OK)
		{
			AddBytes(output, Buffer, BUFFER_SIZE);

			Stream.next_out = Buffer;
			Stream.avail_out = BUFFER_SIZE;
			if (Stream.avail_in != 0)
			{
				err = mz_deflate(&Stream, 0);
			}
		}
		if (err != Z_OK)
			I_FatalError("Error deflating data.");

		while (true)
		{
			err = mz_deflate(&Stream, Z_FINISH);
			if (err != Z_OK)
			{
				break;
			}
			if (Stream.avail_out == 0)
			{
				AddBytes(output, Buffer, BUFFER_SIZE);
				Stream.next_out = Buffer;
				Stream.avail_out = BUFFER_SIZE;
			}
		}
		mz_deflateEnd(&Stream);
		AddBytes(output, Buffer, BUFFER_SIZE - Stream.avail_out);

		return output;
	}

private:
	static void AddBytes(TArray<uint8_t>& output, const void* data, size_t size)
	{
		int index = output.Reserve(size);
		memcpy(&output[index], data, size);
	}

	size_t pos = 0;
	TArray<uint8_t> buffer;
};

class MapLump
{
public:
	FString Name;
	TArray<uint8_t> Data;
};

TArray<MapLump> LoadMapLumps(FileReader* reader, const char* wadType)
{
	char magic[4] = {};
	uint32_t numlumps = 0;
	uint32_t infotableofs = 0;
	if (reader->Read(magic, 4) != 4) return {};
	if (memcmp(magic, wadType, 4) != 0) return {};
	if (reader->Read(&numlumps, 4) != 4) return {};
	if (reader->Read(&infotableofs, 4) != 4) return {};
	if (reader->Seek(infotableofs, FileReader::SeekSet) == -1) return {};

	TArray<MapLump> lumps;
	TArray<uint32_t> offsets;
	lumps.Reserve(numlumps);
	offsets.Reserve(numlumps);

	for (uint32_t i = 0; i < numlumps; i++)
	{
		uint32_t filepos = 0, lumpsize = 0;
		char name[9] = {};
		if (reader->Read(&filepos, 4) != 4) return {};
		if (reader->Read(&lumpsize, 4) != 4) return {};
		if (reader->Read(name, 8) != 8) return {};

		offsets[i] = filepos;
		lumps[i].Name = name;
		lumps[i].Data.Reserve(lumpsize);
	}

	for (uint32_t i = 0; i < numlumps; i++)
	{
		if (reader->Seek(offsets[i], FileReader::SeekSet) == -1) return {};
		if (reader->Read(lumps[i].Data.data(), lumps[i].Data.size()) != lumps[i].Data.size()) return {};
	}

	return lumps;
}

void SaveMapLumps(FileWriter* writer, const TArray<MapLump>& lumps, const char* wadType)
{
	uint32_t numlumps = (uint32_t)lumps.size();
	uint32_t infotableofs = 12;
	for (uint32_t i = 0; i < numlumps; i++)
		infotableofs += lumps[i].Data.size();

	writer->Write(wadType, 4);
	writer->Write(&numlumps, 4);
	writer->Write(&infotableofs, 4);

	TArray<uint32_t> offsets;
	offsets.Reserve(numlumps);

	uint32_t pos = 12;
	for (uint32_t i = 0; i < numlumps; i++)
	{
		offsets[i] = pos;
		writer->Write(lumps[i].Data.data(), lumps[i].Data.size());
		pos += lumps[i].Data.size();
	}

	for (uint32_t i = 0; i < numlumps; i++)
	{
		uint32_t filepos = offsets[i];
		uint32_t lumpsize = lumps[i].Data.size();
		char name[8] = {};
		memcpy(name, lumps[i].Name.GetChars(), lumps[i].Name.Len());
		writer->Write(&filepos, 4);
		writer->Write(&lumpsize, 4);
		writer->Write(name, 8);
	}
}

void DoomLevelMesh::SaveLightmapLump(FLevelLocals& doomMap)
{
	/*
	// LIGHTMAP V3 pseudo-C specification:

	struct LightmapLump
	{
		int version = 2;
		uint32_t tileCount;
		uint32_t pixelCount;
		uint32_t uvCount;
		SurfaceEntry surfaces[surfaceCount];
		uint16_t pixels[pixelCount * 3];
	};

	struct TileEntry
	{
		uint32_t type, typeIndex;
		uint32_t controlSector; // 0xFFFFFFFF is none
		uint16_t width, height; // in pixels
		uint32_t pixelsOffset; // offset in pixels array
		vec3 translateWorldToLocal;
		vec3 projLocalToU;
		vec3 projLocalToV;
	};
	*/

	Lightmap.TextureData.Resize(Lightmap.TextureSize * Lightmap.TextureSize * Lightmap.TextureCount * 4);
	for (int arrayIndex = 0; arrayIndex < Lightmap.TextureCount; arrayIndex++)
	{
		screen->DownloadLightmap(arrayIndex, Lightmap.TextureData.Data() + arrayIndex * Lightmap.TextureSize * Lightmap.TextureSize * 4);
	}

	// Calculate size of lump
	uint32_t tileCount = 0;
	uint32_t pixelCount = 0;

	for (unsigned int i = 0; i < Lightmap.Tiles.Size(); i++)
	{
		LightmapTile* tile = &Lightmap.Tiles[i];
		if (tile->AtlasLocation.ArrayIndex != -1)
		{
			tileCount++;
			pixelCount += tile->AtlasLocation.Area();
		}
	}

	const int version = 3;

	const uint32_t headerSize = sizeof(int) + 2 * sizeof(uint32_t);
	const uint32_t bytesPerTileEntry = sizeof(uint32_t) * 4 + sizeof(uint16_t) * 2 + sizeof(float) * 9;
	const uint32_t bytesPerPixel = sizeof(uint16_t) * 3; // F16 RGB

	uint32_t lumpSize = headerSize + tileCount * bytesPerTileEntry + pixelCount * bytesPerPixel;

	LumpWriter lumpFile(lumpSize);

	// Write header
	lumpFile.Write32(version);
	lumpFile.Write32(tileCount);
	lumpFile.Write32(pixelCount);

	// Write tiles
	uint32_t pixelsOffset = 0;

	for (unsigned int i = 0; i < Lightmap.Tiles.Size(); i++)
	{
		LightmapTile* tile = &Lightmap.Tiles[i];

		if (tile->AtlasLocation.ArrayIndex == -1)
			continue;

		lumpFile.Write32(tile->Binding.Type);
		lumpFile.Write32(tile->Binding.TypeIndex);
		lumpFile.Write32(tile->Binding.ControlSector);

		lumpFile.Write16(uint16_t(tile->AtlasLocation.Width));
		lumpFile.Write16(uint16_t(tile->AtlasLocation.Height));

		lumpFile.Write32(pixelsOffset * 3);

		lumpFile.WriteFloat(tile->Transform.TranslateWorldToLocal.X);
		lumpFile.WriteFloat(tile->Transform.TranslateWorldToLocal.Y);
		lumpFile.WriteFloat(tile->Transform.TranslateWorldToLocal.Z);

		lumpFile.WriteFloat(tile->Transform.ProjLocalToU.X);
		lumpFile.WriteFloat(tile->Transform.ProjLocalToU.Y);
		lumpFile.WriteFloat(tile->Transform.ProjLocalToU.Z);

		lumpFile.WriteFloat(tile->Transform.ProjLocalToV.X);
		lumpFile.WriteFloat(tile->Transform.ProjLocalToV.Y);
		lumpFile.WriteFloat(tile->Transform.ProjLocalToV.Z);

		pixelsOffset += tile->AtlasLocation.Area();
	}

	// Write surface pixels
	for (unsigned int i = 0; i < Lightmap.Tiles.Size(); i++)
	{
		LightmapTile* tile = &Lightmap.Tiles[i];

		if (tile->AtlasLocation.ArrayIndex == -1)
			continue;

		const uint16_t* pixels = Lightmap.TextureData.Data() + tile->AtlasLocation.ArrayIndex * Lightmap.TextureSize * Lightmap.TextureSize * 4;
		int width = tile->AtlasLocation.Width;
		int height = tile->AtlasLocation.Height;
		for (int y = 0; y < height; y++)
		{
			const uint16_t* srcline = pixels + (tile->AtlasLocation.X + (tile->AtlasLocation.Y + y) * Lightmap.TextureSize) * 4;
			for (int x = 0; x < width; x++)
			{
				lumpFile.Write16(*(srcline++));
				lumpFile.Write16(*(srcline++));
				lumpFile.Write16(*(srcline++));
				srcline++;
			}
		}
	}

	FString fullpath = GetMapFilename(doomMap);
	if (fullpath.Len() == 0)
		return;

	Printf("Saving LIGHTMAP lump into %s\n", fullpath.GetChars());

	FileReader reader;
	if (!reader.OpenFile(fullpath.GetChars()))
	{
		I_Error("Could not open map WAD file");
		return;
	}

	TArray<MapLump> lumps = LoadMapLumps(&reader, "PWAD");
	if (lumps.Size() == 0)
	{
		I_Error("Could not read map WAD file");
		return;
	}

	reader.Close();

	int lightmapIndex = -1;
	int endmapIndex = lumps.size();
	FString strLightmap = "LIGHTMAP";
	FString strEndmap = "ENDMAP";
	for (int i = 0, count = lumps.size(); i < count; i++)
	{
		if (lumps[i].Name == strLightmap)
			lightmapIndex = i;
		else if (lumps[i].Name == strEndmap)
			endmapIndex = i;
	}

	if (lightmapIndex != -1)
		lumps[lightmapIndex].Data = lumpFile.DeflateCompress();
	else
		lumps.Insert(endmapIndex, { "LIGHTMAP", lumpFile.DeflateCompress()});

	std::unique_ptr<FileWriter> writer(FileWriter::Open(fullpath.GetChars()));
	if (writer)
	{
		SaveMapLumps(writer.get(), lumps, "PWAD");
	}
	else
	{
		I_Error("Could not write map WAD file");
	}
}

void DoomLevelMesh::DeleteLightmapLump(FLevelLocals& doomMap)
{
	FString fullpath = GetMapFilename(doomMap);
	if (fullpath.Len() == 0)
		return;

	Printf("Deleting LIGHTMAP lump from %s\n", fullpath.GetChars());

	FileReader reader;
	if (!reader.OpenFile(fullpath.GetChars()))
	{
		I_Error("Could not open map WAD file");
		return;
	}

	TArray<MapLump> lumps = LoadMapLumps(&reader, "PWAD");
	if (lumps.Size() == 0)
	{
		I_Error("Could not read map WAD file");
		return;
	}
	reader.Close();

	int lightmapIndex = -1;
	FString strLightmap = "LIGHTMAP";
	for (int i = 0, count = lumps.size(); i < count; i++)
	{
		if (lumps[i].Name == strLightmap)
			lightmapIndex = i;
	}

	if (lightmapIndex == -1)
		return;

	lumps.Delete(lightmapIndex);

	std::unique_ptr<FileWriter> writer(FileWriter::Open(fullpath.GetChars()));
	if (writer)
	{
		SaveMapLumps(writer.get(), lumps, "PWAD");
	}
	else
	{
		I_Error("Could not write map WAD file");
	}
}

FString DoomLevelMesh::GetMapFilename(FLevelLocals& doomMap)
{
	const char* mapname = doomMap.MapName.GetChars();

	FString fmt;
	fmt.Format("maps/%s.wad", mapname);
	int lump_wad = fileSystem.CheckNumForFullName(fmt.GetChars());
	if (lump_wad == -1)
	{
		I_Error("Could not find map lump");
		return {};
	}

	int wadnum = fileSystem.GetFileContainer(lump_wad);
	if (wadnum == -1)
	{
		I_Error("Could not find map folder");
		return {};
	}

	FString filename = fileSystem.GetFileFullName(lump_wad);
	FString folder = fileSystem.GetResourceFileFullName(wadnum);
	FString fullpath = folder + filename;
	return fullpath;
}

// struct lightmap

static void InvalidateActorLightTraceCache()
{
	auto it = level.GetThinkerIterator<AActor>();
	AActor* ac;
	while ((ac = it.Next()))
	{
		ac->InvalidateLightTraceCache();
	}
}

DEFINE_ACTION_FUNCTION(_Lightmap, Invalidate)
{
	PARAM_PROLOGUE;
	InvalidateLightmap();
	InvalidateActorLightTraceCache();
	return 0;
}

DEFINE_ACTION_FUNCTION(_Lightmap, SetSunDirection)
{
	PARAM_PROLOGUE;
	PARAM_FLOAT(ang);
	PARAM_FLOAT(pch);

	auto a = FAngle::fromDeg(float(ang));
	auto p = FAngle::fromDeg(float(pch));
	auto cosp = p.Cos();
	auto vec = -FVector3{ cosp * a.Cos(), cosp * a.Sin(), -p.Sin() };

	if (!vec.isZero() && level.levelMesh)
	{
		vec.MakeUnit();
		level.SunDirection = vec;
		level.levelMesh->SunDirection = vec;
	}
	return 0;
}

DEFINE_ACTION_FUNCTION(_Lightmap, SetSunColor)
{
	PARAM_PROLOGUE;
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_FLOAT(z);

	if (level.levelMesh)
	{
		auto vec = FVector3(float(x), float(y), float(z));
		level.SunColor = vec;
		level.levelMesh->SunColor = vec;
	}
	return 0;
}

