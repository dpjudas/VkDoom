
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

EXTERN_CVAR(Bool, gl_texture)
EXTERN_CVAR(Float, lm_scale);

void DoomLevelSubmesh::CreateStatic(FLevelLocals& doomMap)
{
	LightmapSampleDistance = doomMap.LightmapSampleDistance;

	Reset();
	BuildSectorGroups(doomMap);

	CreateStaticSurfaces(doomMap);
	LinkSurfaces(doomMap);

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
	MeshSurfaceMaterials.Clear();
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

	// Create surface objects for all visible side parts
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

		MeshBuilder state;
		HWMeshHelper result;
		HWWallDispatcher disp(&doomMap, &result, ELightMode::ZDoomSoftware);
		HWWall wall;
		wall.sub = sub;
		wall.Process(&disp, state, seg, front, back);

		// Part 1: solid geometry. This is set up so that there are no transparent parts
		state.SetDepthFunc(DF_Less);
		state.ClearDepthBias();
		state.EnableTexture(gl_texture);
		state.EnableBrightmap(true);

		for (HWWall& wallpart : result.list)
		{
			if (wallpart.texture && wallpart.texture->isMasked())
			{
				state.AlphaFunc(Alpha_GEqual, gl_mask_threshold);
			}
			else
			{
				state.AlphaFunc(Alpha_GEqual, 0.f);
			}

			wallpart.DrawWall(&disp, state, false);

			int pipelineID = 0;
			int startVertIndex = MeshVertices.Size();
			for (auto& it : state.mSortedLists)
			{
				const MeshApplyState& applyState = it.first;

				pipelineID = screen->GetLevelMeshPipelineID(applyState.applyData, applyState.surfaceUniforms, applyState.material);

				int uniformsIndex = MeshSurfaceUniforms.Size();
				MeshSurfaceUniforms.Push(applyState.surfaceUniforms);
				MeshSurfaceMaterials.Push(applyState.material);

				for (MeshDrawCommand& command : it.second.mDraws)
				{
					for (int i = command.Start, end = command.Start + command.Count; i < end; i++)
					{
						MeshVertices.Push(state.mVertices[i]);
						MeshUniformIndexes.Push(uniformsIndex);
					}
				}
			}
			state.mSortedLists.clear();

			DoomLevelMeshSurface surf;
			surf.Submesh = this;
			surf.Type = wallpart.LevelMeshInfo.Type;
			surf.ControlSector = wallpart.LevelMeshInfo.ControlSector;
			surf.TypeIndex = side->Index();
			surf.Side = side;
			surf.AlwaysUpdate = !!(front->Flags & SECF_LM_DYNAMIC);
			surf.sectorGroup = sectorGroup[front->Index()];
			surf.alpha = float(side->linedef->alpha);
			surf.startVertIndex = startVertIndex;
			surf.numVerts = MeshVertices.Size() - startVertIndex;
			surf.plane = ToPlane(MeshVertices[startVertIndex].fPos(), MeshVertices[startVertIndex + 1].fPos(), MeshVertices[startVertIndex + 2].fPos(), MeshVertices[startVertIndex + 3].fPos());
			surf.texture = wallpart.texture;
			surf.PipelineID = pipelineID;
			Surfaces.Push(surf);
		}
	}

	// Create surfaces for all flats
	for (unsigned int i = 0; i < doomMap.subsectors.Size(); i++)
	{
		subsector_t* sub = &doomMap.subsectors[i];
		if (sub->numlines < 3 || !sub->sector)
			continue;
		sector_t* sector = sub->sector;

		CreateFloorSurface(doomMap, sub, sector, nullptr, i);
		CreateCeilingSurface(doomMap, sub, sector, nullptr, i);

		for (unsigned int j = 0; j < sector->e->XFloor.ffloors.Size(); j++)
		{
			CreateFloorSurface(doomMap, sub, sector, sector->e->XFloor.ffloors[j]->model, i);
			CreateCeilingSurface(doomMap, sub, sector, sector->e->XFloor.ffloors[j]->model, i);
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

void DoomLevelSubmesh::CreateIndexes()
{
	// Order indexes by pipeline
	std::unordered_map<int64_t, TArray<int>> pipelineSurfaces;
	for (size_t i = 0; i < Surfaces.Size(); i++)
	{
		DoomLevelMeshSurface* s = &Surfaces[i];
		pipelineSurfaces[(int64_t(s->PipelineID) << 32) | int64_t(s->bSky)].Push(i);
	}

	for (const auto& it : pipelineSurfaces)
	{
		LevelSubmeshDrawRange range;
		range.PipelineID = it.first >> 32;
		range.Start = MeshElements.Size();
		for (unsigned int i : it.second)
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
		range.Count = MeshElements.Size() - range.Start;

		if ((it.first & 1) == 0)
			DrawList.Push(range);
		else
			PortalList.Push(range);
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
	surf.texture = TexMan.GetGameTexture((controlSector ? controlSector : sector)->planes[sector_t::floor].Texture);

	FGameTexture* txt = surf.texture;
	float w = txt->GetDisplayWidth();
	float h = txt->GetDisplayHeight();
	VSMatrix mat = GetPlaneTextureRotationMatrix(txt, sector, sector_t::floor);

	int uniformsIndex = MeshSurfaceUniforms.Size();

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

		MeshUniformIndexes.Push(uniformsIndex);
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
	uniforms.uLightLevel = sector->lightlevel * (1.0f / 255.0f);

	FMaterialState material;
	material.mMaterial = FMaterial::ValidateTexture(txt, 0);
	material.mClampMode = CLAMP_NONE;
	material.mTranslation = 0;
	material.mOverrideShader = -1;
	material.mChanged = true;
	if (material.mMaterial)
	{
		auto scale = material.mMaterial->GetDetailScale();
		uniforms.uDetailParms = { scale.X, scale.Y, 2, 0 };
	}

	MeshSurfaceUniforms.Push(uniforms);
	MeshSurfaceMaterials.Push(material);
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
	surf.texture = TexMan.GetGameTexture((controlSector ? controlSector : sector)->planes[sector_t::ceiling].Texture);

	FGameTexture* txt = surf.texture;
	float w = txt->GetDisplayWidth();
	float h = txt->GetDisplayHeight();
	VSMatrix mat = GetPlaneTextureRotationMatrix(txt, sector, sector_t::ceiling);

	int uniformsIndex = MeshSurfaceUniforms.Size();

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

		MeshUniformIndexes.Push(uniformsIndex);
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
	uniforms.uLightLevel = sector->lightlevel * (1.0f / 255.0f);

	FMaterialState material;
	material.mMaterial = FMaterial::ValidateTexture(txt, 0);
	material.mClampMode = CLAMP_NONE;
	material.mTranslation = 0;
	material.mOverrideShader = -1;
	material.mChanged = true;
	if (material.mMaterial)
	{
		auto scale = material.mMaterial->GetDetailScale();
		uniforms.uDetailParms = { scale.X, scale.Y, 2, 0 };
	}

	MeshSurfaceUniforms.Push(uniforms);
	MeshSurfaceMaterials.Push(material);
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
	surfaceUniforms.uLightLevel = -1.0f;
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
