
#include "templates.h"
#include "doom_levelsubmesh.h"
#include "g_levellocals.h"
#include "texturemanager.h"
#include "playsim/p_lnspec.h"
#include "c_dispatch.h"
#include "g_levellocals.h"
#include "a_dynlight.h"
#include "hw_renderstate.h"
#include "hwrenderer/scene/hw_drawstructs.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hwrenderer/scene/hw_walldispatcher.h"
#include "common/rendering/hwrenderer/data/hw_meshbuilder.h"
#include "common/rendering/vulkan/accelstructs/vk_lightmap.h"
#include "common/rendering/vulkan/accelstructs/halffloat.h"

VSMatrix GetPlaneTextureRotationMatrix(FGameTexture* gltexture, const sector_t* sector, int plane);
void GetTexCoordInfo(FGameTexture* tex, FTexCoordInfo* tci, side_t* side, int texpos);

EXTERN_CVAR(Float, lm_scale);

void DoomLevelSubmesh::CreateStatic(FLevelLocals& doomMap)
{
	LightmapSampleDistance = doomMap.LightmapSampleDistance;

	Reset();
	BuildSectorGroups(doomMap);

	CreateStaticSurfaces(doomMap);
	LinkSurfaces(doomMap);
	ProcessStaticSurfaces(doomMap);

	CreateIndexes();
	SetupLightmapUvs(doomMap);
	BuildTileSurfaceLists();
	UpdateCollision();
}

void DoomLevelSubmesh::CreateDynamic(FLevelLocals& doomMap)
{
	LightmapSampleDistance = doomMap.LightmapSampleDistance;
	Reset();
	BuildSectorGroups(doomMap);
}

void DoomLevelSubmesh::UpdateDynamic(FLevelLocals& doomMap, int lightmapStartIndex)
{
	Reset();

	CreateDynamicSurfaces(doomMap);
	LinkSurfaces(doomMap);
	ProcessStaticSurfaces(doomMap);

	CreateIndexes();
	SetupLightmapUvs(doomMap);
	BuildTileSurfaceLists();
	UpdateCollision();

	if (doomMap.lightmaps)
		PackLightmapAtlas(lightmapStartIndex);
}

void DoomLevelSubmesh::Reset()
{
	Surfaces.Clear();
	MeshVertices.Clear();
	MeshElements.Clear();
	MeshSurfaceIndexes.Clear();
	MeshUniformIndexes.Clear();
	MeshSurfaceUniforms.Clear();
}

void DoomLevelSubmesh::CreateStaticSurfaces(FLevelLocals& doomMap)
{
	// Create surface objects for all visible side parts
	for (unsigned int i = 0; i < doomMap.sides.Size(); i++)
	{
		bool isPolyLine = !!(doomMap.sides[i].Flags & WALLF_POLYOBJ);
		if (!isPolyLine)
		{
			// To do: it would be nice if HWWall could figure this out for us.

			side_t* side = &doomMap.sides[i];

			sector_t* front = side->sector;
			sector_t* back = (side->linedef->frontsector == front) ? side->linedef->backsector : side->linedef->frontsector;

			FVector2 v1 = ToFVector2(side->V1()->fPos());
			FVector2 v2 = ToFVector2(side->V2()->fPos());

			float v1Top = (float)front->ceilingplane.ZatPoint(v1);
			float v1Bottom = (float)front->floorplane.ZatPoint(v1);
			float v2Top = (float)front->ceilingplane.ZatPoint(v2);
			float v2Bottom = (float)front->floorplane.ZatPoint(v2);

			DoomLevelMeshSurface surf;
			surf.Submesh = this;
			surf.TypeIndex = side->Index();
			surf.Side = side;
			surf.AlwaysUpdate = !!(front->Flags & SECF_LM_DYNAMIC);
			surf.sectorGroup = sectorGroup[front->Index()];

			if (side->linedef->getPortal() && side->linedef->frontsector == front) // line portal
			{
				surf.Type = ST_MIDDLESIDE;
				surf.bSky = front->GetTexture(sector_t::floor) == skyflatnum || front->GetTexture(sector_t::ceiling) == skyflatnum;
				surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;
				//surf.plane = ToPlane(verts[0].fPos(), verts[1].fPos(), verts[2].fPos(), verts[3].fPos());
				Surfaces.Push(surf);
			}
			else if (side->linedef->special == Line_Horizon && front != back) // line horizon
			{
				surf.Type = ST_MIDDLESIDE;
				surf.bSky = front->GetTexture(sector_t::floor) == skyflatnum || front->GetTexture(sector_t::ceiling) == skyflatnum;
				surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;
				//surf.plane = ToPlane(verts[0].fPos(), verts[1].fPos(), verts[2].fPos(), verts[3].fPos());
				Surfaces.Push(surf);
			}
			else if (!back) // front wall
			{
				surf.Type = ST_MIDDLESIDE;
				surf.bSky = false;
				surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;
				surf.texture = side->textures[side_t::mid].texture;
				Surfaces.Push(surf);
			}
			else
			{
				if (side->textures[side_t::mid].texture.isValid()) // mid wall
				{
					surf.Type = ST_MIDDLESIDE;
					surf.bSky = false;
					surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;
					surf.texture = side->textures[side_t::mid].texture;
					surf.alpha = float(side->linedef->alpha);
					Surfaces.Push(surf);
				}

				float v1TopBack = (float)back->ceilingplane.ZatPoint(v1);
				float v1BottomBack = (float)back->floorplane.ZatPoint(v1);
				float v2TopBack = (float)back->ceilingplane.ZatPoint(v2);
				float v2BottomBack = (float)back->floorplane.ZatPoint(v2);

				if (v1Bottom < v1BottomBack || v2Bottom < v2BottomBack) // lower wall
				{
					surf.Type = ST_LOWERSIDE;
					surf.bSky = false;
					surf.sampleDimension = side->textures[side_t::bottom].LightmapSampleDistance;
					surf.texture = side->textures[side_t::bottom].texture;
					Surfaces.Push(surf);
				}

				if (v1Top > v1TopBack || v2Top > v2TopBack) // top wall
				{
					bool bSky = IsTopSideSky(front, back, side);
					if (bSky || IsTopSideVisible(side))
					{
						surf.Type = ST_UPPERSIDE;
						surf.bSky = bSky;
						surf.sampleDimension = side->textures[side_t::top].LightmapSampleDistance;
						surf.texture = side->textures[side_t::top].texture;
						Surfaces.Push(surf);
					}
				}

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

					surf.Type = ST_MIDDLESIDE;
					surf.ControlSector = xfloor->model;
					surf.bSky = false;
					surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;
					surf.texture = side->textures[side_t::mid].texture;
					Surfaces.Push(surf);
				}
			}
		}
	}

	// Create surfaces for all flats
	for (unsigned int i = 0; i < doomMap.subsectors.Size(); i++)
	{
		subsector_t* sub = &doomMap.subsectors[i];
		if (sub->numlines < 3 || !sub->sector)
			continue;
		sector_t* sector = sub->sector;

		// To do: only create the surface objects here and then fill them in ProcessStaticSurfaces

		CreateFloorSurface(doomMap, sub, sector, nullptr, i);
		CreateCeilingSurface(doomMap, sub, sector, nullptr, i);

		for (unsigned int j = 0; j < sector->e->XFloor.ffloors.Size(); j++)
		{
			CreateFloorSurface(doomMap, sub, sector, sector->e->XFloor.ffloors[j]->model, i);
			CreateCeilingSurface(doomMap, sub, sector, sector->e->XFloor.ffloors[j]->model, i);
		}
	}
}

void DoomLevelSubmesh::ProcessStaticSurfaces(FLevelLocals& doomMap)
{
	for (unsigned int i = 0; i < doomMap.sides.Size(); i++)
	{
		side_t* side = &doomMap.sides[i];
		bool isPolyLine = !!(side->Flags & WALLF_POLYOBJ);
		if (isPolyLine)
			continue;

		sector_t* front = side->sector;
		sector_t* back = (side->linedef->frontsector == front) ? side->linedef->backsector : side->linedef->frontsector;

#if 0
		MeshBuilder state;
		HWMeshHelper result;
		HWWallDispatcher disp(&doomMap, &result, ELightMode::ZDoomSoftware);
		HWWall wall;
		wall.Process(&disp, state, side->segs[0], front, back);
		for (HWWall& wallpart : result.list)
		{
			wallpart.DrawWall(disp, state, false);
			state.AddToSurface(wallpart.surface);

			/*
			surf.plane = ToPlane(verts[0].fPos(), verts[1].fPos(), verts[2].fPos(), verts[3].fPos());

			FFlatVertex verts[4];

			MeshVertices.Push(verts[0]);
			MeshVertices.Push(verts[1]);
			MeshVertices.Push(verts[2]);
			MeshVertices.Push(verts[3]);

			int surfaceIndex = Surfaces.Size();
			MeshUniformIndexes.Push(surfaceIndex);
			MeshUniformIndexes.Push(surfaceIndex);
			MeshUniformIndexes.Push(surfaceIndex);
			MeshUniformIndexes.Push(surfaceIndex);

			SurfaceUniforms uniforms = DefaultUniforms();
			uniforms.uLightLevel = front->lightlevel;
			MeshSurfaceUniforms.Push(uniforms);
			Surfaces.Push(surf);
			*/
		}
#endif
	}

	for (unsigned int i = 0; i < doomMap.subsectors.Size(); i++)
	{
		subsector_t* sub = &doomMap.subsectors[i];
		if (sub->numlines < 3 || !sub->sector)
			continue;
		sector_t* sector = sub->sector;

		// To do: use HWFlat
	}
}

void DoomLevelSubmesh::CreateDynamicSurfaces(FLevelLocals& doomMap)
{
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
}

void DoomLevelSubmesh::ProcessDynamicSurfaces(FLevelLocals& doomMap)
{
}

void DoomLevelSubmesh::CreateIndexes()
{
	for (size_t i = 0; i < Surfaces.Size(); i++)
	{
		DoomLevelMeshSurface& s = Surfaces[i];
		int numVerts = s.numVerts;
		unsigned int pos = s.startVertIndex;
		FFlatVertex* verts = &MeshVertices[pos];

		s.Vertices = verts;
		s.startElementIndex = MeshElements.Size();
		s.numElements = 0;

		if (s.Type == ST_CEILING)
		{
			for (int j = 2; j < numVerts; j++)
			{
				if (!IsDegenerate(verts[0].fPos(), verts[j - 1].fPos(), verts[j].fPos()))
				{
					MeshElements.Push(pos);
					MeshElements.Push(pos + j - 1);
					MeshElements.Push(pos + j);
					MeshSurfaceIndexes.Push((int)i);
					s.numElements += 3;
				}
			}
		}
		else if (s.Type == ST_FLOOR)
		{
			for (int j = 2; j < numVerts; j++)
			{
				if (!IsDegenerate(verts[0].fPos(), verts[j - 1].fPos(), verts[j].fPos()))
				{
					MeshElements.Push(pos + j);
					MeshElements.Push(pos + j - 1);
					MeshElements.Push(pos);
					MeshSurfaceIndexes.Push((int)i);
					s.numElements += 3;
				}
			}
		}
		else if (s.Type == ST_MIDDLESIDE || s.Type == ST_UPPERSIDE || s.Type == ST_LOWERSIDE)
		{
			if (!IsDegenerate(verts[0].fPos(), verts[2].fPos(), verts[1].fPos()))
			{
				MeshElements.Push(pos + 0);
				MeshElements.Push(pos + 1);
				MeshElements.Push(pos + 2);
				MeshSurfaceIndexes.Push((int)i);
				s.numElements += 3;
			}
			if (!IsDegenerate(verts[0].fPos(), verts[2].fPos(), verts[3].fPos()))
			{
				MeshElements.Push(pos + 0);
				MeshElements.Push(pos + 2);
				MeshElements.Push(pos + 3);
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

void DoomLevelSubmesh::CreateLinePortalSurface(FLevelLocals& doomMap, side_t* side)
{
	sector_t* front = side->sector;

	FVector2 v1 = ToFVector2(side->V1()->fPos());
	FVector2 v2 = ToFVector2(side->V2()->fPos());

	float v1Top = (float)front->ceilingplane.ZatPoint(v1);
	float v1Bottom = (float)front->floorplane.ZatPoint(v1);
	float v2Top = (float)front->ceilingplane.ZatPoint(v2);
	float v2Bottom = (float)front->floorplane.ZatPoint(v2);

	FFlatVertex verts[4];
	verts[0].x = verts[2].x = v1.X;
	verts[0].y = verts[2].y = v1.Y;
	verts[1].x = verts[3].x = v2.X;
	verts[1].y = verts[3].y = v2.Y;
	verts[0].z = v1Bottom;
	verts[1].z = v2Bottom;
	verts[2].z = v1Top;
	verts[3].z = v2Top;

	DoomLevelMeshSurface surf;
	surf.Submesh = this;
	surf.Type = ST_MIDDLESIDE;
	surf.TypeIndex = side->Index();
	surf.bSky = front->GetTexture(sector_t::floor) == skyflatnum || front->GetTexture(sector_t::ceiling) == skyflatnum;
	surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;
	surf.startVertIndex = MeshVertices.Size();
	surf.numVerts = 4;
	surf.Side = side;

	MeshVertices.Push(verts[0]);
	MeshVertices.Push(verts[1]);
	MeshVertices.Push(verts[2]);
	MeshVertices.Push(verts[3]);

	int surfaceIndex = Surfaces.Size();
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);

	surf.plane = ToPlane(verts[0].fPos(), verts[1].fPos(), verts[2].fPos(), verts[3].fPos());
	surf.sectorGroup = sectorGroup[front->Index()];
	surf.AlwaysUpdate = !!(front->Flags & SECF_LM_DYNAMIC);

	SurfaceUniforms uniforms = DefaultUniforms();
	uniforms.uLightLevel = front->lightlevel;

	SetSideTextureUVs(surf, side, side_t::top, v1Top, v1Bottom, v2Top, v2Bottom);

	MeshSurfaceUniforms.Push(uniforms);
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
	surf.Side = side;

	FFlatVertex verts[4];
	verts[0].x = verts[2].x = v1.X;
	verts[0].y = verts[2].y = v1.Y;
	verts[1].x = verts[3].x = v2.X;
	verts[1].y = verts[3].y = v2.Y;
	verts[0].z = v1Bottom;
	verts[1].z = v2Bottom;
	verts[2].z = v1Top;
	verts[3].z = v2Top;

	surf.startVertIndex = MeshVertices.Size();
	surf.numVerts = 4;
	MeshVertices.Push(verts[0]);
	MeshVertices.Push(verts[2]);
	MeshVertices.Push(verts[3]);
	MeshVertices.Push(verts[1]);

	int surfaceIndex = Surfaces.Size();
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);

	surf.plane = ToPlane(verts[0].fPos(), verts[1].fPos(), verts[2].fPos(), verts[3].fPos());
	surf.sectorGroup = sectorGroup[front->Index()];
	surf.AlwaysUpdate = !!(front->Flags & SECF_LM_DYNAMIC);

	SurfaceUniforms uniforms = DefaultUniforms();
	uniforms.uLightLevel = front->lightlevel;

	SetSideTextureUVs(surf, side, side_t::top, v1Top, v1Bottom, v2Top, v2Bottom);

	MeshSurfaceUniforms.Push(uniforms);
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

	FFlatVertex verts[4];
	verts[0].x = verts[2].x = v1.X;
	verts[0].y = verts[2].y = v1.Y;
	verts[1].x = verts[3].x = v2.X;
	verts[1].y = verts[3].y = v2.Y;
	verts[0].z = v1Bottom;
	verts[1].z = v2Bottom;
	verts[2].z = v1Top;
	verts[3].z = v2Top;

	DoomLevelMeshSurface surf;
	surf.Submesh = this;
	surf.bSky = false;
	surf.startVertIndex = MeshVertices.Size();
	surf.numVerts = 4;
	surf.bSky = false;
	MeshVertices.Push(verts[0]);
	MeshVertices.Push(verts[2]);
	MeshVertices.Push(verts[3]);
	MeshVertices.Push(verts[1]);

	int surfaceIndex = Surfaces.Size();
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);

	surf.plane = ToPlane(verts[0].fPos(), verts[1].fPos(), verts[2].fPos(), verts[3].fPos());
	surf.Type = ST_MIDDLESIDE;
	surf.TypeIndex = side->Index();
	surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;
	surf.ControlSector = nullptr;
	surf.sectorGroup = sectorGroup[front->Index()];
	surf.texture = side->textures[side_t::mid].texture;
	surf.AlwaysUpdate = !!(front->Flags & SECF_LM_DYNAMIC);
	surf.Side = side;

	SurfaceUniforms uniforms = DefaultUniforms();
	uniforms.uLightLevel = front->lightlevel;

	SetSideTextureUVs(surf, side, side_t::top, v1Top, v1Bottom, v2Top, v2Bottom);

	MeshSurfaceUniforms.Push(uniforms);
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

	FFlatVertex verts[4];
	verts[0].x = verts[2].x = v1.X;
	verts[0].y = verts[2].y = v1.Y;
	verts[1].x = verts[3].x = v2.X;
	verts[1].y = verts[3].y = v2.Y;

	const auto& texture = side->textures[side_t::mid].texture;

	if ((side->Flags & WALLF_WRAP_MIDTEX) || (side->linedef->flags & WALLF_WRAP_MIDTEX))
	{
		verts[0].z = v1Bottom;
		verts[1].z = v2Bottom;
		verts[2].z = v1Top;
		verts[3].z = v2Top;
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

		verts[0].z = min(max(yTextureOffset + mid1Bottom, v1Bottom), v1Top);
		verts[1].z = min(max(yTextureOffset + mid2Bottom, v2Bottom), v2Top);
		verts[2].z = max(min(yTextureOffset + mid1Top, v1Top), v1Bottom);
		verts[3].z = max(min(yTextureOffset + mid2Top, v2Top), v2Bottom);
	}

	// mid texture
	DoomLevelMeshSurface surf;
	surf.Submesh = this;
	surf.bSky = false;
	surf.startVertIndex = MeshVertices.Size();
	surf.numVerts = 4;
	surf.bSky = false;
	surf.plane = ToPlane(verts[0].fPos(), verts[1].fPos(), verts[2].fPos(), verts[3].fPos());

	if (side->linedef->sidedef[0] != side)
	{
		surf.plane = -surf.plane;
		surf.plane.W = -surf.plane.W;
	}

	MeshVertices.Push(verts[0]);
	MeshVertices.Push(verts[2]);
	MeshVertices.Push(verts[3]);
	MeshVertices.Push(verts[1]);

	int surfaceIndex = Surfaces.Size();
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);

	surf.Type = ST_MIDDLESIDE;
	surf.TypeIndex = side->Index();
	surf.sampleDimension = side->textures[side_t::mid].LightmapSampleDistance;
	surf.ControlSector = nullptr;
	surf.sectorGroup = sectorGroup[front->Index()];
	surf.texture = texture;
	surf.alpha = float(side->linedef->alpha);
	surf.AlwaysUpdate = !!(front->Flags & SECF_LM_DYNAMIC);
	surf.Side = side;

	SurfaceUniforms uniforms = DefaultUniforms();
	uniforms.uLightLevel = front->lightlevel;

	SetSideTextureUVs(surf, side, side_t::top, verts[2].z, verts[0].z, verts[3].z, verts[1].z);

	MeshSurfaceUniforms.Push(uniforms);
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
		surf.Side = side;

		float blZ = (float)xfloor->model->floorplane.ZatPoint(v1);
		float brZ = (float)xfloor->model->floorplane.ZatPoint(v2);
		float tlZ = (float)xfloor->model->ceilingplane.ZatPoint(v1);
		float trZ = (float)xfloor->model->ceilingplane.ZatPoint(v2);

		FFlatVertex verts[4];
		verts[0].x = verts[2].x = v2.X;
		verts[0].y = verts[2].y = v2.Y;
		verts[1].x = verts[3].x = v1.X;
		verts[1].y = verts[3].y = v1.Y;
		verts[0].z = brZ;
		verts[1].z = blZ;
		verts[2].z = trZ;
		verts[3].z = tlZ;

		surf.startVertIndex = MeshVertices.Size();
		surf.numVerts = 4;
		MeshVertices.Push(verts[0]);
		MeshVertices.Push(verts[2]);
		MeshVertices.Push(verts[3]);
		MeshVertices.Push(verts[1]);

		int surfaceIndex = Surfaces.Size();
		MeshUniformIndexes.Push(surfaceIndex);
		MeshUniformIndexes.Push(surfaceIndex);
		MeshUniformIndexes.Push(surfaceIndex);
		MeshUniformIndexes.Push(surfaceIndex);

		surf.plane = ToPlane(verts[0].fPos(), verts[1].fPos(), verts[2].fPos(), verts[3].fPos());
		surf.sectorGroup = sectorGroup[front->Index()];
		surf.texture = side->textures[side_t::mid].texture;
		surf.AlwaysUpdate = !!(front->Flags & SECF_LM_DYNAMIC);

		SurfaceUniforms uniforms = DefaultUniforms();
		uniforms.uLightLevel = front->lightlevel;

		SetSideTextureUVs(surf, side, side_t::top, tlZ, blZ, trZ, brZ);

		MeshSurfaceUniforms.Push(uniforms);
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

	FFlatVertex verts[4];
	verts[0].x = verts[2].x = v1.X;
	verts[0].y = verts[2].y = v1.Y;
	verts[1].x = verts[3].x = v2.X;
	verts[1].y = verts[3].y = v2.Y;
	verts[0].z = v1TopBack;
	verts[1].z = v2TopBack;
	verts[2].z = v1Top;
	verts[3].z = v2Top;

	DoomLevelMeshSurface surf;
	surf.Submesh = this;
	surf.startVertIndex = MeshVertices.Size();
	surf.numVerts = 4;
	MeshVertices.Push(verts[0]);
	MeshVertices.Push(verts[2]);
	MeshVertices.Push(verts[3]);
	MeshVertices.Push(verts[1]);

	int surfaceIndex = Surfaces.Size();
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);

	surf.plane = ToPlane(verts[0].fPos(), verts[1].fPos(), verts[2].fPos(), verts[3].fPos());
	surf.Type = ST_UPPERSIDE;
	surf.TypeIndex = side->Index();
	surf.bSky = bSky;
	surf.sampleDimension = side->textures[side_t::top].LightmapSampleDistance;
	surf.ControlSector = nullptr;
	surf.sectorGroup = sectorGroup[front->Index()];
	surf.texture = side->textures[side_t::top].texture;
	surf.AlwaysUpdate = !!(front->Flags & SECF_LM_DYNAMIC);
	surf.Side = side;

	SurfaceUniforms uniforms = DefaultUniforms();
	uniforms.uLightLevel = front->lightlevel;

	SetSideTextureUVs(surf, side, side_t::top, v1Top, v1TopBack, v2Top, v2TopBack);

	MeshSurfaceUniforms.Push(uniforms);
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

	FFlatVertex verts[4];
	verts[0].x = verts[2].x = v1.X;
	verts[0].y = verts[2].y = v1.Y;
	verts[1].x = verts[3].x = v2.X;
	verts[1].y = verts[3].y = v2.Y;
	verts[0].z = v1Bottom;
	verts[1].z = v2Bottom;
	verts[2].z = v1BottomBack;
	verts[3].z = v2BottomBack;

	DoomLevelMeshSurface surf;
	surf.Submesh = this;
	surf.startVertIndex = MeshVertices.Size();
	surf.numVerts = 4;
	MeshVertices.Push(verts[0]);
	MeshVertices.Push(verts[2]);
	MeshVertices.Push(verts[3]);
	MeshVertices.Push(verts[1]);

	int surfaceIndex = Surfaces.Size();
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);
	MeshUniformIndexes.Push(surfaceIndex);

	surf.plane = ToPlane(verts[0].fPos(), verts[1].fPos(), verts[2].fPos(), verts[3].fPos());
	surf.Type = ST_LOWERSIDE;
	surf.TypeIndex = side->Index();
	surf.bSky = false;
	surf.sampleDimension = side->textures[side_t::bottom].LightmapSampleDistance;
	surf.ControlSector = nullptr;
	surf.sectorGroup = sectorGroup[front->Index()];
	surf.texture = side->textures[side_t::bottom].texture;
	surf.AlwaysUpdate = !!(front->Flags & SECF_LM_DYNAMIC);
	surf.Side = side;

	SurfaceUniforms uniforms = DefaultUniforms();
	uniforms.uLightLevel = front->lightlevel;

	SetSideTextureUVs(surf, side, side_t::bottom, v1BottomBack, v1Bottom, v2BottomBack, v2Bottom);

	MeshSurfaceUniforms.Push(uniforms);
	Surfaces.Push(surf);
}

void DoomLevelSubmesh::SetSideTextureUVs(DoomLevelMeshSurface& surface, side_t* side, side_t::ETexpart texpart, float v1TopZ, float v1BottomZ, float v2TopZ, float v2BottomZ)
{
	FFlatVertex* uvs = &MeshVertices[surface.startVertIndex];

	if (surface.texture.isValid())
	{
		const auto gtxt = TexMan.GetGameTexture(surface.texture);

		FTexCoordInfo tci;
		GetTexCoordInfo(gtxt, &tci, side, texpart);

		float startU = tci.FloatToTexU(tci.TextureOffset((float)side->GetTextureXOffset(texpart)) + tci.TextureOffset((float)side->GetTextureXOffset(texpart)));
		float endU = startU + tci.FloatToTexU(side->TexelLength);

		uvs[0].u = startU;
		uvs[1].u = endU;
		uvs[3].u = startU;
		uvs[2].u = endU;

		// To do: the ceiling version is apparently used in some situation related to 3d floors (rover->top.isceiling)
		//float offset = tci.RowOffset((float)side->GetTextureYOffset(texpart)) + tci.RowOffset((float)side->GetTextureYOffset(texpart)) + (float)side->sector->GetPlaneTexZ(sector_t::ceiling);
		float offset = tci.RowOffset((float)side->GetTextureYOffset(texpart)) + tci.RowOffset((float)side->GetTextureYOffset(texpart)) + (float)side->sector->GetPlaneTexZ(sector_t::floor);

		uvs[0].v = tci.FloatToTexV(offset - v1BottomZ);
		uvs[1].v = tci.FloatToTexV(offset - v2BottomZ);
		uvs[3].v = tci.FloatToTexV(offset - v1TopZ);
		uvs[2].v = tci.FloatToTexV(offset - v2TopZ);
	}
	else
	{
		uvs[0].u = 0.0f;
		uvs[0].v = 0.0f;
		uvs[1].u = 0.0f;
		uvs[1].v = 0.0f;
		uvs[2].u = 0.0f;
		uvs[2].v = 0.0f;
		uvs[3].u = 0.0f;
		uvs[3].v = 0.0f;
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

	int surfaceIndex = Surfaces.Size();

	MeshVertices.Resize(surf.startVertIndex + surf.numVerts);

	FFlatVertex* verts = &MeshVertices[surf.startVertIndex];

	for (int j = 0; j < surf.numVerts; j++)
	{
		seg_t* seg = &sub->firstline[j];
		FVector2 v1 = ToFVector2(seg->v1->fPos());
		FVector2 uv = (mat * FVector4(v1.X / 64.f, -v1.Y / 64.f, 0.f, 1.f)).XY(); // The magic 64.f and negative Y is based on SetFlatVertex

		verts[j].x = v1.X;
		verts[j].y = v1.Y;
		verts[j].z = (float)plane.ZatPoint(v1);
		verts[j].u = uv.X;
		verts[j].v = uv.Y;

		MeshUniformIndexes.Push(surfaceIndex);
	}

	surf.Type = ST_FLOOR;
	surf.TypeIndex = typeIndex;
	surf.sampleDimension = (controlSector ? controlSector : sector)->planes[sector_t::floor].LightmapSampleDistance;
	surf.ControlSector = controlSector;
	surf.plane = FVector4((float)plane.Normal().X, (float)plane.Normal().Y, (float)plane.Normal().Z, -(float)plane.D);
	surf.sectorGroup = sectorGroup[sector->Index()];
	surf.AlwaysUpdate = !!(sector->Flags & SECF_LM_DYNAMIC);
	surf.Subsector = sub;

	SurfaceUniforms uniforms = DefaultUniforms();
	uniforms.uLightLevel = sector->lightlevel;

	MeshSurfaceUniforms.Push(uniforms);
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

	int surfaceIndex = Surfaces.Size();

	MeshVertices.Resize(surf.startVertIndex + surf.numVerts);

	FFlatVertex* verts = &MeshVertices[surf.startVertIndex];

	for (int j = 0; j < surf.numVerts; j++)
	{
		seg_t* seg = &sub->firstline[j];
		FVector2 v1 = ToFVector2(seg->v1->fPos());
		FVector2 uv = (mat * FVector4(v1.X / 64.f, -v1.Y / 64.f, 0.f, 1.f)).XY(); // The magic 64.f and negative Y is based on SetFlatVertex

		verts[j].x = v1.X;
		verts[j].y = v1.Y;
		verts[j].z = (float)plane.ZatPoint(v1);
		verts[j].u = uv.X;
		verts[j].v = uv.Y;

		MeshUniformIndexes.Push(surfaceIndex);
	}

	surf.Type = ST_CEILING;
	surf.TypeIndex = typeIndex;
	surf.sampleDimension = (controlSector ? controlSector : sector)->planes[sector_t::ceiling].LightmapSampleDistance;
	surf.ControlSector = controlSector;
	surf.plane = FVector4((float)plane.Normal().X, (float)plane.Normal().Y, (float)plane.Normal().Z, -(float)plane.D);
	surf.sectorGroup = sectorGroup[sector->Index()];
	surf.AlwaysUpdate = !!(sector->Flags & SECF_LM_DYNAMIC);
	surf.Subsector = sub;

	SurfaceUniforms uniforms = DefaultUniforms();
	uniforms.uLightLevel = sector->lightlevel;

	MeshSurfaceUniforms.Push(uniforms);
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

void DoomLevelSubmesh::SetupLightmapUvs(FLevelLocals& doomMap)
{
	LMTextureSize = 1024;

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
			auto& vertex = MeshVertices[surf->startVertIndex + i];
			vertex.lu = (vertex.lu + x) / (float)LMTextureSize;
			vertex.lv = (vertex.lv + y) / (float)LMTextureSize;
			vertex.lindex = (float)surf->AtlasTile.ArrayIndex;
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
			if (MeshVertices[i].fPos()[j] < low[j])
			{
				low[j] = MeshVertices[i].fPos()[j];
			}
			if (MeshVertices[i].fPos()[j] > hi[j])
			{
				hi[j] = MeshVertices[i].fPos()[j];
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

	for (int i = 0; i < surface.numVerts; i++)
	{
		FVector3 tDelta = MeshVertices[surface.startVertIndex + i].fPos() - surface.translateWorldToLocal;

		MeshVertices[surface.startVertIndex + i].lu = (tDelta | surface.projLocalToU);
		MeshVertices[surface.startVertIndex + i].lv = (tDelta | surface.projLocalToV);
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

SurfaceUniforms DoomLevelSubmesh::DefaultUniforms()
{
	SurfaceUniforms surfaceUniforms = {};
	surfaceUniforms.uFogColor = toFVector4(PalEntry(0xffffffff));
	surfaceUniforms.uDesaturationFactor = 0.0f;
	surfaceUniforms.uAlphaThreshold = 0.5f;
	surfaceUniforms.uAddColor = toFVector4(PalEntry(0));
	surfaceUniforms.uObjectColor = toFVector4(PalEntry(0xffffffff));
	surfaceUniforms.uObjectColor2 = toFVector4(PalEntry(0));
	surfaceUniforms.uTextureBlendColor = toFVector4(PalEntry(0));
	surfaceUniforms.uTextureAddColor = toFVector4(PalEntry(0));
	surfaceUniforms.uTextureModulateColor = toFVector4(PalEntry(0));
	surfaceUniforms.uLightDist = 0.0f;
	surfaceUniforms.uLightFactor = 0.0f;
	surfaceUniforms.uFogDensity = 0.0f;
	surfaceUniforms.uLightLevel = 255.0f;// -1.0f;
	surfaceUniforms.uInterpolationFactor = 0;
	surfaceUniforms.uVertexColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	surfaceUniforms.uGlowTopColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	surfaceUniforms.uGlowBottomColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	surfaceUniforms.uGlowTopPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
	surfaceUniforms.uGlowBottomPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
	surfaceUniforms.uGradientTopPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
	surfaceUniforms.uGradientBottomPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
	surfaceUniforms.uSplitTopPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
	surfaceUniforms.uSplitBottomPlane = { 0.0f, 0.0f, 0.0f, 0.0f };
	surfaceUniforms.uDynLightColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	surfaceUniforms.uDetailParms = { 0.0f, 0.0f, 0.0f, 0.0f };
#ifdef NPOT_EMULATION
	surfaceUniforms.uNpotEmulation = { 0,0,0,0 };
#endif
	surfaceUniforms.uClipSplit.X = -1000000.f;
	surfaceUniforms.uClipSplit.Y = 1000000.f;
	return surfaceUniforms;
}
