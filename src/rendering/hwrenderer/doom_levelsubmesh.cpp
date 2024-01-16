
#include "templates.h"
#include "doom_levelsubmesh.h"
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
#include "common/rendering/hwrenderer/data/hw_meshbuilder.h"
#include <unordered_map>

EXTERN_CVAR(Float, lm_scale);

DoomLevelSubmesh::DoomLevelSubmesh(DoomLevelMesh* mesh, FLevelLocals& doomMap, bool staticMesh) : LevelMesh(mesh), StaticMesh(staticMesh)
{
	LightmapSampleDistance = doomMap.LightmapSampleDistance;
	Reset();

	if (StaticMesh)
	{
		CreateStaticSurfaces(doomMap);
		LinkSurfaces(doomMap);

		SortIndexes();
		BuildTileSurfaceLists();
		UpdateCollision();
	}
}

void DoomLevelSubmesh::Update(FLevelLocals& doomMap)
{
	if (!StaticMesh)
	{
		Reset();

		CreateDynamicSurfaces(doomMap);
		LinkSurfaces(doomMap);

		SortIndexes();
		BuildTileSurfaceLists();
		UpdateCollision();
	}
}

void DoomLevelSubmesh::Reset()
{
	Surfaces.Clear();
	Portals.Clear();
	Mesh.Vertices.Clear();
	Mesh.Indexes.Clear();
	Mesh.SurfaceIndexes.Clear();
	Mesh.UniformIndexes.Clear();
	Mesh.Uniforms.Clear();
	Mesh.Materials.Clear();
}

void DoomLevelSubmesh::CreateStaticSurfaces(FLevelLocals& doomMap)
{
	// We can't use side->segs since it is null.
	TArray<std::pair<subsector_t*, seg_t*>> sideSegs(doomMap.sides.Size(), true);
	for (unsigned int i = 0; i < doomMap.subsectors.Size(); i++)
	{
		subsector_t* sub = &doomMap.subsectors[i];
		sector_t* sector = sub->sector;
		for (int i = 0, count = sub->numlines; i < count; i++)
		{
			seg_t* seg = sub->firstline + i;
			if (seg->sidedef)
				sideSegs[seg->sidedef->Index()] = { sub, seg };
		}
	}

	MeshBuilder state;
	std::map<LightmapTileBinding, int> bindings;

	// Create surface objects for all sides
	for (unsigned int i = 0; i < doomMap.sides.Size(); i++)
	{
		side_t* side = &doomMap.sides[i];
		bool isPolyLine = !!(side->Flags & WALLF_POLYOBJ);
		if (isPolyLine)
			continue;

		subsector_t* sub = sideSegs[i].first;
		seg_t* seg = sideSegs[i].second;
		if (!seg)
			continue;

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
		CreateWallSurface(side, disp, state, bindings, result.list, false, true);

		for (HWWall& portal : result.portals)
		{
			Portals.Push(portal);
		}

		CreateWallSurface(side, disp, state, bindings, result.portals, true, false);

		/*
		// final pass: translucent stuff
		state.AlphaFunc(Alpha_GEqual, gl_mask_sprite_threshold);
		state.SetRenderStyle(STYLE_Translucent);
		CreateWallSurface(side, disp, state, bindings, result.translucent, false, true);
		state.AlphaFunc(Alpha_GEqual, 0.f);
		state.SetRenderStyle(STYLE_Normal);
		*/
	}

	// Create surfaces for all flats
	for (unsigned int i = 0; i < doomMap.sectors.Size(); i++)
	{
		sector_t* sector = &doomMap.sectors[i];
		if (sector->subsectors[0]->flags & SSECF_POLYORG)
			continue;
		for (FSection& section : doomMap.sections.SectionsForSector(i))
		{
			int sectionIndex = doomMap.sections.SectionIndex(&section);

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
			CreateFlatSurface(disp, state, bindings, result.list);

			CreateFlatSurface(disp, state, bindings, result.portals, true);

			// final pass: translucent stuff
			state.AlphaFunc(Alpha_GEqual, gl_mask_sprite_threshold);
			state.SetRenderStyle(STYLE_Translucent);
			CreateFlatSurface(disp, state, bindings, result.translucentborder);
			state.SetDepthMask(false);
			CreateFlatSurface(disp, state, bindings, result.translucent);
			state.AlphaFunc(Alpha_GEqual, 0.f);
			state.SetDepthMask(true);
			state.SetRenderStyle(STYLE_Normal);
		}
	}
}

void DoomLevelSubmesh::CreateWallSurface(side_t* side, HWWallDispatcher& disp, MeshBuilder& state, std::map<LightmapTileBinding, int>& bindings, TArray<HWWall>& list, bool isSky, bool translucent)
{
	for (HWWall& wallpart : list)
	{
		if (isSky)
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

		int pipelineID = 0;
		int startVertIndex = Mesh.Vertices.Size();
		int startElementIndex = Mesh.Indexes.Size();
		for (auto& it : state.mSortedLists)
		{
			const MeshApplyState& applyState = it.first;

			pipelineID = screen->GetLevelMeshPipelineID(applyState.applyData, applyState.surfaceUniforms, applyState.material);

			int uniformsIndex = Mesh.Uniforms.Size();
			Mesh.Uniforms.Push(applyState.surfaceUniforms);
			Mesh.Materials.Push(applyState.material);

			for (MeshDrawCommand& command : it.second.mDraws)
			{
				for (int i = command.Start, end = command.Start + command.Count; i < end; i++)
				{
					Mesh.Vertices.Push(state.mVertices[i]);
					Mesh.UniformIndexes.Push(uniformsIndex);
				}

				if (command.DrawType == DT_TriangleFan)
				{
					for (int i = 2, count = command.Count; i < count; i++)
					{
						Mesh.Indexes.Push(startVertIndex);
						Mesh.Indexes.Push(startVertIndex + i - 1);
						Mesh.Indexes.Push(startVertIndex + i);
					}
				}
			}
		}
		state.mSortedLists.clear();
		state.mVertices.Clear();
		state.mIndexes.Clear();

		FVector2 v1 = FVector2(side->V1()->fPos());
		FVector2 v2 = FVector2(side->V2()->fPos());
		FVector2 N = FVector2(v2.Y - v1.Y, v1.X - v2.X).Unit();

		DoomLevelMeshSurface surf;
		surf.Submesh = this;
		surf.Type = wallpart.LevelMeshInfo.Type;
		surf.ControlSector = wallpart.LevelMeshInfo.ControlSector;
		surf.TypeIndex = side->Index();
		surf.Side = side;
		surf.AlwaysUpdate = !!(side->sector->Flags & SECF_LM_DYNAMIC);
		surf.SectorGroup = LevelMesh->sectorGroup[side->sector->Index()];
		surf.Alpha = float(side->linedef->alpha);
		surf.MeshLocation.StartVertIndex = startVertIndex;
		surf.MeshLocation.StartElementIndex = startElementIndex;
		surf.MeshLocation.NumVerts = Mesh.Vertices.Size() - startVertIndex;
		surf.MeshLocation.NumElements = Mesh.Indexes.Size() - startElementIndex;
		surf.Plane = FVector4(N.X, N.Y, 0.0f, v1 | N);
		surf.Texture = wallpart.texture;
		surf.PipelineID = pipelineID;
		surf.PortalIndex = isSky ? LevelMesh->linePortals[side->linedef->Index()] : 0;
		surf.IsSky = isSky;
		surf.Bounds = GetBoundsFromSurface(surf);
		surf.LightmapTileIndex = disp.Level->lightmaps ? AddSurfaceToTile(surf, bindings) : -1;
		Surfaces.Push(surf);
	}
}

int DoomLevelSubmesh::AddSurfaceToTile(const DoomLevelMeshSurface& surf, std::map<LightmapTileBinding, int>& bindings)
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

		return index;
	}
	else
	{
		int index = LightmapTiles.Size();

		LightmapTile tile;
		tile.Binding = binding;
		tile.Bounds = surf.Bounds;
		tile.Plane = surf.Plane;
		tile.SampleDimension = GetSampleDimension(surf);

		LightmapTiles.Push(tile);
		bindings[binding] = index;
		return index;
	}
}

int DoomLevelSubmesh::GetSampleDimension(const DoomLevelMeshSurface& surf)
{
	uint16_t sampleDimension = 0; // To do: something seems to have gone missing with the sample dimension!

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

void DoomLevelSubmesh::CreateFlatSurface(HWFlatDispatcher& disp, MeshBuilder& state, std::map<LightmapTileBinding, int>& bindings, TArray<HWFlat>& list, bool isSky)
{
	for (HWFlat& flatpart : list)
	{
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

			flatpart.DrawFlat(&disp, state, false);
		}

		VSMatrix textureMatrix;
		textureMatrix.loadIdentity();

		int pipelineID = 0;
		int uniformsIndex = 0;
		bool foundDraw = false;
		for (auto& it : state.mSortedLists)
		{
			const MeshApplyState& applyState = it.first;

			pipelineID = screen->GetLevelMeshPipelineID(applyState.applyData, applyState.surfaceUniforms, applyState.material);
			uniformsIndex = Mesh.Uniforms.Size();
			textureMatrix = applyState.textureMatrix;
			Mesh.Uniforms.Push(applyState.surfaceUniforms);
			Mesh.Materials.Push(applyState.material);

			foundDraw = true;
			break;
		}
		state.mSortedLists.clear();
		state.mVertices.Clear();
		state.mIndexes.Clear();

		if (!foundDraw)
			continue;

		DoomLevelMeshSurface surf;
		surf.Submesh = this;
		surf.Type = flatpart.ceiling ? ST_CEILING : ST_FLOOR;
		surf.ControlSector = flatpart.controlsector ? flatpart.controlsector->model : nullptr;
		surf.AlwaysUpdate = !!(flatpart.sector->Flags & SECF_LM_DYNAMIC);
		surf.SectorGroup = LevelMesh->sectorGroup[flatpart.sector->Index()];
		surf.Alpha = flatpart.alpha;
		surf.Texture = flatpart.texture;
		surf.PipelineID = pipelineID;
		surf.PortalIndex = LevelMesh->sectorPortals[flatpart.ceiling][flatpart.sector->Index()];
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

			int startVertIndex = Mesh.Vertices.Size();
			int startElementIndex = Mesh.Indexes.Size();

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

				Mesh.Vertices.Push(ffv);
				Mesh.UniformIndexes.Push(uniformsIndex);
			}

			if (flatpart.ceiling)
			{
				for (int i = 2, count = sub->numlines; i < count; i++)
				{
					Mesh.Indexes.Push(startVertIndex);
					Mesh.Indexes.Push(startVertIndex + i - 1);
					Mesh.Indexes.Push(startVertIndex + i);
				}
			}
			else
			{
				for (int i = 2, count = sub->numlines; i < count; i++)
				{
					Mesh.Indexes.Push(startVertIndex + i);
					Mesh.Indexes.Push(startVertIndex + i - 1);
					Mesh.Indexes.Push(startVertIndex);
				}
			}

			surf.TypeIndex = sub->Index();
			surf.Subsector = sub;
			surf.MeshLocation.StartVertIndex = startVertIndex;
			surf.MeshLocation.StartElementIndex = startElementIndex;
			surf.MeshLocation.NumVerts = sub->numlines;
			surf.MeshLocation.NumElements = (sub->numlines - 2) * 3;
			surf.Bounds = GetBoundsFromSurface(surf);
			surf.LightmapTileIndex = disp.Level->lightmaps ? AddSurfaceToTile(surf, bindings) : -1;
			Surfaces.Push(surf);
		}
	}
}

void DoomLevelSubmesh::CreateDynamicSurfaces(FLevelLocals& doomMap)
{
#if 0
	// Look for polyobjects
	for (unsigned int i = 0; i < doomMap.lines.Size(); i++)
	{
		side_t* side = doomMap.lines[i].sidedef[0];
		bool isPolyLine = !!(side->Flags & WALLF_POLYOBJ);
		if (!isPolyLine)
			continue;

		// Make sure we have a surface array on the polyobj sidedef
		if (!side->surface)
		{
			auto array = std::make_unique<DoomLevelMeshSurface * []>(4);
			memset(array.get(), 0, sizeof(DoomLevelMeshSurface*));
			side->surface = array.get();
			PolyLMSurfaces.Push(std::move(array));
		}

		CreateSideSurfaces(doomMap, side);
	}
#endif
}

void DoomLevelSubmesh::SortIndexes()
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

void DoomLevelSubmesh::LinkSurfaces(FLevelLocals& doomMap)
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

void DoomLevelSubmesh::SetSubsectorLightmap(DoomLevelMeshSurface* surface)
{
	if (surface->Subsector->firstline && surface->Subsector->firstline->sidedef)
		surface->Subsector->firstline->sidedef->sector->HasLightmaps = true;

	if (!surface->ControlSector)
	{
		int index = surface->Type == ST_CEILING ? 1 : 0;
		surface->Subsector->surface[index][0] = surface;
	}
	else
	{
		int index = surface->Type == ST_CEILING ? 0 : 1;
		const auto& ffloors = surface->Subsector->sector->e->XFloor.ffloors;
		for (unsigned int i = 0; i < ffloors.Size(); i++)
		{
			if (ffloors[i]->model == surface->ControlSector)
			{
				surface->Subsector->surface[index][i + 1] = surface;
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
			surface->Side->surface[0] = surface;
		}
		else if (surface->Type == ST_MIDDLESIDE)
		{
			surface->Side->surface[1] = surface;
			surface->Side->surface[2] = surface;
		}
		else if (surface->Type == ST_LOWERSIDE)
		{
			surface->Side->surface[3] = surface;
		}
	}
	else
	{
		const auto& ffloors = surface->Side->sector->e->XFloor.ffloors;
		for (unsigned int i = 0; i < ffloors.Size(); i++)
		{
			if (ffloors[i]->model == surface->ControlSector)
			{
				surface->Side->surface[4 + i] = surface;
			}
		}
	}
}

BBox DoomLevelSubmesh::GetBoundsFromSurface(const LevelMeshSurface& surface) const
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
