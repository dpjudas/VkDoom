
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
#include "hwrenderer/scene/hw_drawstructs.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hwrenderer/scene/hw_walldispatcher.h"
#include "hwrenderer/scene/hw_flatdispatcher.h"
#include <unordered_map>

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
	for (auto& tile : level.levelMesh->LightmapTiles)
	{
		if (!tile.NeedsUpdate)
			++count;
		tile.NeedsUpdate = true;
	}
	Printf("Marked %d out of %d tiles for update.\n", count, level.levelMesh->LightmapTiles.Size());
}

void PrintSurfaceInfo(const DoomLevelMeshSurface* surface)
{
	if (!RequireLevelMesh()) return;

	auto gameTexture = surface->Texture;

	Printf("Surface %d (%p)\n    Type: %d, TypeIndex: %d, ControlSector: %d\n", level.levelMesh->GetSurfaceIndex(surface), surface, surface->Type, surface->TypeIndex, surface->ControlSector ? surface->ControlSector->Index() : -1);
	if (surface->LightmapTileIndex >= 0)
	{
		LightmapTile* tile = &level.levelMesh->LightmapTiles[surface->LightmapTileIndex];
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
	// Remove the empty mesh added in the LevelMesh constructor
	Mesh.Vertices.clear();
	Mesh.Indexes.clear();

	SunColor = doomMap.SunColor; // TODO keep only one copy?
	SunDirection = doomMap.SunDirection;

	BuildSectorGroups(doomMap);
	CreatePortals(doomMap);

	LightmapSampleDistance = doomMap.LightmapSampleDistance;

	CreateSurfaces(doomMap);
	LinkSurfaces(doomMap);

	SortIndexes();
	BuildTileSurfaceLists();

	UpdateCollision();

	// Assume double the size of the static mesh will be enough for anything dynamic.
	Mesh.MaxVertices = std::max(Mesh.Vertices.size() * 2, (size_t)10000);
	Mesh.MaxIndexes = std::max(Mesh.Indexes.size() * 2, (size_t)10000);
	Mesh.MaxSurfaces = std::max(Mesh.SurfaceIndexes.size() * 2, (size_t)10000);
	Mesh.MaxUniforms = std::max(Mesh.Uniforms.size() * 2, (size_t)10000);
	Mesh.MaxSurfaceIndexes = std::max(Mesh.SurfaceIndexes.size() * 2, (size_t)10000);
	Mesh.MaxNodes = std::max(Collision->get_nodes().size() * 2, (size_t)10000);
	Mesh.MaxLights = 100'000;
	Mesh.MaxLightIndexes = 4 * 1024 * 1024;

	UploadAll();
}

void DoomLevelMesh::CreateLights(FLevelLocals& doomMap)
{
	if (Mesh.Lights.Size() != 0)
		return;

	for (DoomLevelMeshSurface& surface : Surfaces)
	{
		surface.LightList.Pos = Mesh.LightIndexes.Size();
		surface.LightList.Count = 0;

		std::pair<FLightNode*, int> nodePortalGroup = GetSurfaceLightNode(&surface);
		FLightNode* node = nodePortalGroup.first;
		int portalgroup = nodePortalGroup.second;
		if (!node)
			continue;

		int listpos = 0;
		while (node)
		{
			FDynamicLight* light = node->lightsource;
			if (light && light->Trace())
			{
				int lightindex = GetLightIndex(light, portalgroup);
				if (lightindex >= 0)
				{
					AddRange(UploadRanges.LightIndex, { (int)Mesh.LightIndexes.Size(), 1 });
					Mesh.LightIndexes.Push(lightindex);
					surface.LightList.Count++;
				}
			}
			node = node->nextLight;
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

	DVector3 pos = light->PosRelative(portalgroup);

	LevelMeshLight meshlight;
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

	int lightindex = Mesh.Lights.Size();
	light->levelmesh[index].index = lightindex + 1;
	light->levelmesh[index].portalgroup = portalgroup;
	AddRange(UploadRanges.Light, { (int)Mesh.Lights.Size(), 1 });
	Mesh.Lights.Push(meshlight);
	return lightindex;
}

void DoomLevelMesh::BeginFrame(FLevelLocals& doomMap)
{
	CreateLights(doomMap);
}

bool DoomLevelMesh::TraceSky(const FVector3& start, FVector3 direction, float dist)
{
	FVector3 end = start + direction * dist;
	auto surface = Trace(start, direction, dist);
	return surface && surface->IsSky;
}

std::pair<FLightNode*, int> DoomLevelMesh::GetSurfaceLightNode(const DoomLevelMeshSurface* doomsurf)
{
	FLightNode* node = nullptr;
	int portalgroup = 0;
	if (doomsurf->Type == ST_FLOOR || doomsurf->Type == ST_CEILING)
	{
		node = doomsurf->Subsector->section->lighthead;
		portalgroup = doomsurf->Subsector->sector->PortalGroup;
	}
	else if (doomsurf->Type == ST_MIDDLESIDE || doomsurf->Type == ST_UPPERSIDE || doomsurf->Type == ST_LOWERSIDE)
	{
		bool isPolyLine = !!(doomsurf->Side->Flags & WALLF_POLYOBJ);
		if (isPolyLine)
		{
			subsector_t* subsector = level.PointInRenderSubsector((doomsurf->Side->V1()->fPos() + doomsurf->Side->V2()->fPos()) * 0.5);
			node = subsector->section->lighthead;
			portalgroup = subsector->sector->PortalGroup;
		}
		else
		{
			node = doomsurf->Side->lighthead;
			portalgroup = doomsurf->Side->sector->PortalGroup;
		}
	}
	return { node, portalgroup };
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
			continue;

		UpdateSide(doomMap, i);
	}

	// Create surfaces for all flats
	for (unsigned int i = 0; i < doomMap.sectors.Size(); i++)
	{
		sector_t* sector = &doomMap.sectors[i];
		if (sector->subsectorcount == 0 || sector->subsectors[0]->flags & SSECF_POLYORG)
			continue;
		UpdateFlat(doomMap, i);
	}
}

void DoomLevelMesh::FreeSide(FLevelLocals& doomMap, unsigned int sideIndex)
{
	if (sideIndex < 0 || sideIndex >= Sides.Size())
		return;

	int surf = Sides[sideIndex].FirstSurface;
	while (surf != -1)
	{
		unsigned int next = Surfaces[surf].NextSurface;
		FreeSurface(surf);
		surf = next;
	}
	Sides[sideIndex].FirstSurface = -1;

	// To do: call FreeGeometry and FreeUniforms
}

void DoomLevelMesh::FreeFlat(FLevelLocals& doomMap, unsigned int sectorIndex)
{
	if (sectorIndex < 0 || sectorIndex >= Flats.Size())
		return;

	int surf = Flats[sectorIndex].FirstSurface;
	while (surf != -1)
	{
		unsigned int next = Surfaces[surf].NextSurface;
		FreeSurface(surf - 1);
		surf = next;
	}
	Flats[sectorIndex].FirstSurface = -1;

	// To do: call FreeGeometry and FreeUniforms
}

void DoomLevelMesh::FloorHeightChanged(struct sector_t* sector)
{
	// UpdateFlat(level, sector->Index());
}

void DoomLevelMesh::CeilingHeightChanged(struct sector_t* sector)
{
	// UpdateFlat(level, sector->Index());
}

void DoomLevelMesh::MidTex3DHeightChanged(struct sector_t* sector)
{
	// UpdateFlat(level, sector->Index());
}

void DoomLevelMesh::FloorTextureChanged(struct sector_t* sector)
{
	// UpdateFlat(level, sector->Index());
}

void DoomLevelMesh::CeilingTextureChanged(struct sector_t* sector)
{
	// UpdateFlat(level, sector->Index());
}

void DoomLevelMesh::SectorChangedOther(struct sector_t* sector)
{
	// UpdateFlat(level, sector->Index());
}

void DoomLevelMesh::SideTextureChanged(struct side_t* side, int section)
{
	// UpdateSide(level, side->Index());
}

void DoomLevelMesh::SectorLightChanged(struct sector_t* sector)
{
};

void DoomLevelMesh::SectorLightThinkerCreated(struct sector_t* sector, class DLighting* lightthinker)
{
};

void DoomLevelMesh::SectorLightThinkerDestroyed(struct sector_t* sector, class DLighting* lightthinker)
{
};

void DoomLevelMesh::UpdateSide(FLevelLocals& doomMap, unsigned int sideIndex)
{
	FreeSide(doomMap, sideIndex);

	side_t* side = &doomMap.sides[sideIndex];
	seg_t* seg = side->segs[0];
	if (!seg)
		return;

	subsector_t* sub = seg->Subsector;

	sector_t* front = side->sector;
	sector_t* back = (side->linedef->frontsector == front) ? side->linedef->backsector : side->linedef->frontsector;

	HWMeshHelper result;
	HWWallDispatcher disp(&doomMap, &result, getRealLightmode(&doomMap, true));
	HWWall wall;
	wall.sub = sub;
	wall.Process(&disp, state, seg, front, back);

	// Part 1: solid geometry. This is set up so that there are no transparent parts
	state.SetDepthFunc(DF_LEqual);
	state.ClearDepthBias();
	state.EnableTexture(true);
	state.EnableBrightmap(true);
	state.AlphaFunc(Alpha_GEqual, 0.f);
	CreateWallSurface(side, disp, state, result.list, false, true, sideIndex);

	for (HWWall& portal : result.portals)
	{
		WallPortals.Push(portal);
	}

	CreateWallSurface(side, disp, state, result.portals, true, false, sideIndex);

	/*
	// final pass: translucent stuff
	state.AlphaFunc(Alpha_GEqual, gl_mask_sprite_threshold);
	state.SetRenderStyle(STYLE_Translucent);
	CreateWallSurface(side, disp, state, result.translucent, false, true, sideIndex);
	state.AlphaFunc(Alpha_GEqual, 0.f);
	state.SetRenderStyle(STYLE_Normal);
	*/
}

void DoomLevelMesh::UpdateFlat(FLevelLocals& doomMap, unsigned int sectorIndex)
{
	FreeFlat(doomMap, sectorIndex);

	sector_t* sector = &doomMap.sectors[sectorIndex];
	for (FSection& section : doomMap.sections.SectionsForSector(sectorIndex))
	{
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
		CreateFlatSurface(disp, state, result.list, false, false, sectorIndex);

		CreateFlatSurface(disp, state, result.portals, true, false, sectorIndex);

		// final pass: translucent stuff
		state.AlphaFunc(Alpha_GEqual, gl_mask_sprite_threshold);
		state.SetRenderStyle(STYLE_Translucent);
		CreateFlatSurface(disp, state, result.translucentborder, false, true, sectorIndex);
		state.SetDepthMask(false);
		CreateFlatSurface(disp, state, result.translucent, false, true, sectorIndex);
		state.AlphaFunc(Alpha_GEqual, 0.f);
		state.SetDepthMask(true);
		state.SetRenderStyle(STYLE_Normal);
	}
}

void DoomLevelMesh::CreateWallSurface(side_t* side, HWWallDispatcher& disp, MeshBuilder& state, TArray<HWWall>& list, bool isPortal, bool translucent, unsigned int sideIndex)
{
	for (HWWall& wallpart : list)
	{
		if (isPortal)
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

			*(uinfo.Uniforms++) = applyState.surfaceUniforms;
			*(uinfo.Materials++) = applyState.material;
			uniformsIndex++;
		}
		state.mSortedLists.clear();
		state.mVertices.Clear();
		state.mIndexes.Clear();

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

		DoomLevelMeshSurface surf;
		surf.Type = wallpart.LevelMeshInfo.Type;
		surf.ControlSector = wallpart.LevelMeshInfo.ControlSector;
		surf.TypeIndex = side->Index();
		surf.Side = side;
		surf.SectorGroup = sectorGroup[side->sector->Index()];
		surf.Alpha = float(side->linedef->alpha);
		surf.MeshLocation.StartVertIndex = ginfo.VertexStart;
		surf.MeshLocation.StartElementIndex = ginfo.IndexStart;
		surf.MeshLocation.NumVerts = ginfo.VertexCount;
		surf.MeshLocation.NumElements = ginfo.IndexCount;
		surf.Plane = FVector4(N.X, N.Y, 0.0f, v1 | N);
		surf.Texture = wallpart.texture;
		surf.PipelineID = pipelineID;
		surf.PortalIndex = isPortal ? linePortals[side->linedef->Index()] : 0;
		surf.IsSky = isPortal ? (wallpart.portaltype == PORTALTYPE_SKY || wallpart.portaltype == PORTALTYPE_SKYBOX || wallpart.portaltype == PORTALTYPE_HORIZON) : false;
		surf.Bounds = GetBoundsFromSurface(surf);
		surf.LightmapTileIndex = disp.Level->lightmaps ? AddSurfaceToTile(surf, sampleDimension, !!(side->sector->Flags & SECF_LM_DYNAMIC)) : -1;

		if (sideIndex < Sides.Size())
		{
			surf.NextSurface = Sides[sideIndex].FirstSurface;
			Sides[sideIndex].FirstSurface = Surfaces.Size();
		}

		Surfaces.Push(surf);
	}
}

int DoomLevelMesh::AddSurfaceToTile(const DoomLevelMeshSurface& surf, uint16_t sampleDimension, bool alwaysUpdate)
{
	if (surf.IsSky)
		return -1;

	LightmapTileBinding binding;
	binding.Type = surf.Type;
	binding.TypeIndex = surf.TypeIndex;
	binding.ControlSector = surf.ControlSector ? surf.ControlSector->Index() : (int)0xffffffffUL;

	auto it = bindings.find(binding);
	if (it != bindings.end())
	{
		int index = it->second;

		LightmapTile& tile = LightmapTiles[index];
		tile.Bounds.min.X = std::min(tile.Bounds.min.X, surf.Bounds.min.X);
		tile.Bounds.min.Y = std::min(tile.Bounds.min.Y, surf.Bounds.min.Y);
		tile.Bounds.min.Z = std::min(tile.Bounds.min.Z, surf.Bounds.min.Z);
		tile.Bounds.max.X = std::max(tile.Bounds.max.X, surf.Bounds.max.X);
		tile.Bounds.max.Y = std::max(tile.Bounds.max.Y, surf.Bounds.max.Y);
		tile.Bounds.max.Z = std::max(tile.Bounds.max.Z, surf.Bounds.max.Z);
		tile.AlwaysUpdate = tile.AlwaysUpdate || alwaysUpdate;

		return index;
	}
	else
	{
		int index = LightmapTiles.Size();

		LightmapTile tile;
		tile.Binding = binding;
		tile.Bounds = surf.Bounds;
		tile.Plane = surf.Plane;
		tile.SampleDimension = GetSampleDimension(surf, sampleDimension);
		tile.AlwaysUpdate = alwaysUpdate;

		LightmapTiles.Push(tile);
		bindings[binding] = index;
		return index;
	}
}

int DoomLevelMesh::GetSampleDimension(const DoomLevelMeshSurface& surf, uint16_t sampleDimension)
{
	if (sampleDimension <= 0)
	{
		sampleDimension = LightmapSampleDistance;
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

void DoomLevelMesh::CreateFlatSurface(HWFlatDispatcher& disp, MeshBuilder& state, TArray<HWFlat>& list, bool isSky, bool translucent, unsigned int sectorIndex)
{
	for (HWFlat& flatpart : list)
	{
		state.mSortedLists.clear();
		state.mVertices.Clear();
		state.mIndexes.Clear();

		if (isSky)
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

		*uinfo.Uniforms = *uniforms;
		*uinfo.Materials = *material;

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

		DoomLevelMeshSurface surf;
		surf.Type = flatpart.ceiling ? ST_CEILING : ST_FLOOR;
		surf.ControlSector = flatpart.controlsector ? flatpart.controlsector->model : nullptr;
		surf.SectorGroup = sectorGroup[flatpart.sector->Index()];
		surf.Alpha = flatpart.alpha;
		surf.Texture = flatpart.texture;
		surf.PipelineID = pipelineID;
		surf.PortalIndex = sectorPortals[flatpart.ceiling][flatpart.sector->Index()];
		surf.IsSky = isSky;

		auto plane = surf.ControlSector ? surf.ControlSector->GetSecPlane(!flatpart.ceiling) : flatpart.sector->GetSecPlane(flatpart.ceiling);
		surf.Plane = FVector4((float)plane.Normal().X, (float)plane.Normal().Y, (float)plane.Normal().Z, -(float)plane.D);

		if (surf.ControlSector)
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

				FVector3 pt((float)vt->fX(), (float)vt->fY(), isSky ? skyZ : (float)plane.ZatPoint(vt));
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

			if (flatpart.ceiling)
			{
				for (int i = 2, count = sub->numlines; i < count; i++)
				{
					*(ginfo.Indexes++) = startVertIndex;
					*(ginfo.Indexes++) = startVertIndex + i - 1;
					*(ginfo.Indexes++) = startVertIndex + i;
				}
			}
			else
			{
				for (int i = 2, count = sub->numlines; i < count; i++)
				{
					*(ginfo.Indexes++) = startVertIndex + i;
					*(ginfo.Indexes++) = startVertIndex + i - 1;
					*(ginfo.Indexes++) = startVertIndex;
				}
			}

			surf.TypeIndex = sub->Index();
			surf.Subsector = sub;
			surf.MeshLocation.StartVertIndex = startVertIndex;
			surf.MeshLocation.StartElementIndex = startElementIndex;
			surf.MeshLocation.NumVerts = sub->numlines;
			surf.MeshLocation.NumElements = (sub->numlines - 2) * 3;
			surf.Bounds = GetBoundsFromSurface(surf);
			surf.LightmapTileIndex = disp.Level->lightmaps ? AddSurfaceToTile(surf, sampleDimension, !!(flatpart.sector->Flags & SECF_LM_DYNAMIC)) : -1;

			if (sectorIndex < Flats.Size())
			{
				surf.NextSurface = Flats[sectorIndex].FirstSurface;
				Flats[sectorIndex].FirstSurface = Surfaces.Size();
			}

			Surfaces.Push(surf);
		}
	}
}

void DoomLevelMesh::SortIndexes()
{
	// Order surfaces by pipeline
	std::unordered_map<int64_t, TArray<int>> pipelineSurfaces;
	for (size_t i = 0; i < Surfaces.Size(); i++)
	{
		DoomLevelMeshSurface* s = &Surfaces[i];
		pipelineSurfaces[(int64_t(s->PipelineID) << 32) | int64_t(s->IsSky)].Push(i);
	}

	// Create reorder surface indexes by pipeline and create a draw range for each
	TArray<uint32_t> sortedIndexes;
	for (const auto& it : pipelineSurfaces)
	{
		LevelSubmeshDrawRange range;
		range.PipelineID = it.first >> 32;
		range.Start = sortedIndexes.Size();

		// Move indexes to new array
		for (unsigned int i : it.second)
		{
			DoomLevelMeshSurface& s = Surfaces[i];

			unsigned int start = s.MeshLocation.StartElementIndex;
			unsigned int count = s.MeshLocation.NumElements;

			s.MeshLocation.StartElementIndex = sortedIndexes.Size();

			for (unsigned int j = 0; j < count; j++)
			{
				sortedIndexes.Push(Mesh.Indexes[start + j]);
			}

			for (unsigned int j = 0; j < count; j += 3)
			{
				Mesh.SurfaceIndexes.Push((int)i);
			}
		}

		range.Count = sortedIndexes.Size() - range.Start;

		if ((it.first & 1) == 0)
			DrawList.Push(range);
		else
			PortalList.Push(range);
	}

	Mesh.Indexes.Swap(sortedIndexes);
}

void DoomLevelMesh::LinkSurfaces(FLevelLocals& doomMap)
{
	for (auto& surface : Surfaces)
	{
		if (surface.Type == ST_FLOOR || surface.Type == ST_CEILING)
		{
			SetSubsectorLightmap(&surface);
		}
		else
		{
			SetSideLightmap(&surface);
		}
	}
}

void DoomLevelMesh::SetSubsectorLightmap(DoomLevelMeshSurface* surface)
{
	if (surface->Subsector->firstline && surface->Subsector->firstline->sidedef)
		surface->Subsector->firstline->sidedef->sector->HasLightmaps = true;

	if (!surface->ControlSector)
	{
		int index = surface->Type == ST_CEILING ? 1 : 0;
		surface->Subsector->LightmapTiles[index][0] = surface->LightmapTileIndex;
	}
	else
	{
		int index = surface->Type == ST_CEILING ? 0 : 1;
		const auto& ffloors = surface->Subsector->sector->e->XFloor.ffloors;
		for (unsigned int i = 0; i < ffloors.Size(); i++)
		{
			if (ffloors[i]->model == surface->ControlSector)
			{
				surface->Subsector->LightmapTiles[index][i + 1] = surface->LightmapTileIndex;
			}
		}
	}
}

void DoomLevelMesh::SetSideLightmap(DoomLevelMeshSurface* surface)
{
	if (!surface->ControlSector)
	{
		if (surface->Type == ST_UPPERSIDE)
		{
			surface->Side->LightmapTiles[0] = surface->LightmapTileIndex;
		}
		else if (surface->Type == ST_MIDDLESIDE)
		{
			surface->Side->LightmapTiles[1] = surface->LightmapTileIndex;
			surface->Side->LightmapTiles[2] = surface->LightmapTileIndex;
		}
		else if (surface->Type == ST_LOWERSIDE)
		{
			surface->Side->LightmapTiles[3] = surface->LightmapTileIndex;
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
				backside->LightmapTiles[4 + i] = surface->LightmapTileIndex;
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

void DoomLevelMesh::DumpMesh(const FString& objFilename, const FString& mtlFilename) const
{
	auto f = fopen(objFilename.GetChars(), "w");

	fprintf(f, "# DoomLevelMesh debug export\n");
	fprintf(f, "# Vertices: %u, Indexes: %u, Surfaces: %u\n", Mesh.Vertices.Size(), Mesh.Indexes.Size(), Surfaces.Size());
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

	for (unsigned i = 0, count = Mesh.Indexes.Size(); i + 2 < count; i += 3)
	{
		auto index = Mesh.SurfaceIndexes[i / 3];

		if (index != lastSurfaceIndex)
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
				fprintf(f, "o Surface[%d] %s %d%s\n", index, name(surface.Type), surface.TypeIndex, surface.IsSky ? " sky" : "");

				if (surface.LightmapTileIndex >= 0)
				{
					auto& tile = LightmapTiles[surface.LightmapTileIndex];
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
