
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
	WallPortals.Clear();
	Mesh.Vertices.Clear();
	Mesh.Elements.Clear();
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

		HWMeshHelper result;
		HWWallDispatcher disp(&doomMap, &result, ELightMode::ZDoomSoftware);
		HWWall wall;
		wall.sub = sub;
		wall.Process(&disp, state, seg, front, back);

		// Part 1: solid geometry. This is set up so that there are no transparent parts
		state.SetDepthFunc(DF_LEqual);
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
			int startVertIndex = Mesh.Vertices.Size();
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
				}
				for (MeshDrawCommand& command : it.second.mIndexedDraws)
				{
					for (int i = command.Start, end = command.Start + command.Count; i < end; i++)
					{
						Mesh.Vertices.Push(state.mVertices[state.mIndexes[i]]);
						Mesh.UniformIndexes.Push(uniformsIndex);
					}
				}
			}
			state.mSortedLists.clear();
			state.mVertices.Clear();
			state.mIndexes.Clear();

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
			surf.numVerts = Mesh.Vertices.Size() - startVertIndex;
			surf.plane = ToPlane(Mesh.Vertices[startVertIndex].fPos(), Mesh.Vertices[startVertIndex + 1].fPos(), Mesh.Vertices[startVertIndex + 2].fPos(), Mesh.Vertices[startVertIndex + 3].fPos());
			surf.texture = wallpart.texture;
			surf.PipelineID = pipelineID;
			Surfaces.Push(surf);
		}

		for (const HWWall& portal : result.portals)
		{
			WallPortals.Push(portal);
		}
	}

	// Create surfaces for all flats
	for (unsigned int i = 0; i < doomMap.sectors.Size(); i++)
	{
		sector_t* sector = &doomMap.sectors[i];
		for (FSection& section : doomMap.sections.SectionsForSector(i))
		{
			int sectionIndex = doomMap.sections.SectionIndex(&section);

			HWFlatMeshHelper result;
			HWFlatDispatcher disp(&doomMap, &result, ELightMode::ZDoomSoftware);

			HWFlat flat;
			flat.section = &section;
			flat.ProcessSector(&disp, state, sector);

			// Part 1: solid geometry. This is set up so that there are no transparent parts
			state.SetDepthFunc(DF_LEqual);
			state.ClearDepthBias();
			state.EnableTexture(gl_texture);
			state.EnableBrightmap(true);

			for (HWFlat& flatpart : result.list)
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

				int pipelineID = 0;
				int uniformsIndex = 0;
				bool foundDraw = false;
				for (auto& it : state.mSortedLists)
				{
					const MeshApplyState& applyState = it.first;

					pipelineID = screen->GetLevelMeshPipelineID(applyState.applyData, applyState.surfaceUniforms, applyState.material);
					uniformsIndex = Mesh.Uniforms.Size();
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

				auto plane = sector->GetSecPlane(flatpart.ceiling);

				DoomLevelMeshSurface surf;
				surf.Submesh = this;
				surf.Type = flatpart.ceiling ? ST_CEILING : ST_FLOOR;
				surf.ControlSector = flatpart.controlsector ? flatpart.controlsector->model : nullptr;
				surf.AlwaysUpdate = !!(sector->Flags & SECF_LM_DYNAMIC);
				surf.sectorGroup = sectorGroup[sector->Index()];
				surf.alpha = flatpart.alpha;
				surf.texture = flatpart.texture;
				surf.PipelineID = pipelineID;
				surf.plane = FVector4((float)plane.Normal().X, (float)plane.Normal().Y, (float)plane.Normal().Z, -(float)plane.D);

				for (subsector_t* sub : section.subsectors)
				{
					int startVertIndex = Mesh.Vertices.Size();

					for (int i = 0, end = sub->numlines; i < end; i++)
					{
						auto& vt = sub->firstline[end - 1 - i].v1;

						FFlatVertex ffv;
						ffv.x = (float)vt->fX();
						ffv.y = (float)vt->fY();
						ffv.z = (float)plane.ZatPoint(vt);
						ffv.u = (float)vt->fX() / 64.f;
						ffv.v = -(float)vt->fY() / 64.f;
						ffv.lu = 0.0f;
						ffv.lv = 0.0f;
						ffv.lindex = -1.0f;

						Mesh.Vertices.Push(ffv);
						Mesh.UniformIndexes.Push(uniformsIndex);
					}

					surf.TypeIndex = sub->Index();
					surf.Subsector = sub;
					surf.startVertIndex = startVertIndex;
					surf.numVerts = sub->numlines;
					Surfaces.Push(surf);
				}
			}
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
		range.Start = Mesh.Elements.Size();
		for (unsigned int i : it.second)
		{
			DoomLevelMeshSurface& s = Surfaces[i];
			int numVerts = s.numVerts;
			unsigned int pos = s.startVertIndex;
			FFlatVertex* verts = &Mesh.Vertices[pos];

			s.Vertices = verts;
			s.startElementIndex = Mesh.Elements.Size();
			s.numElements = 0;

			if (s.Type == ST_CEILING)
			{
				for (int j = 2; j < numVerts; j++)
				{
					if (!IsDegenerate(verts[0].fPos(), verts[j - 1].fPos(), verts[j].fPos()))
					{
						Mesh.Elements.Push(pos);
						Mesh.Elements.Push(pos + j - 1);
						Mesh.Elements.Push(pos + j);
						Mesh.SurfaceIndexes.Push((int)i);
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
						Mesh.Elements.Push(pos + j);
						Mesh.Elements.Push(pos + j - 1);
						Mesh.Elements.Push(pos);
						Mesh.SurfaceIndexes.Push((int)i);
						s.numElements += 3;
					}
				}
			}
			else if (s.Type == ST_MIDDLESIDE || s.Type == ST_UPPERSIDE || s.Type == ST_LOWERSIDE)
			{
				if (!IsDegenerate(verts[0].fPos(), verts[2].fPos(), verts[1].fPos()))
				{
					Mesh.Elements.Push(pos + 0);
					Mesh.Elements.Push(pos + 1);
					Mesh.Elements.Push(pos + 2);
					Mesh.SurfaceIndexes.Push((int)i);
					s.numElements += 3;
				}
				if (!IsDegenerate(verts[0].fPos(), verts[2].fPos(), verts[3].fPos()))
				{
					Mesh.Elements.Push(pos + 0);
					Mesh.Elements.Push(pos + 2);
					Mesh.Elements.Push(pos + 3);
					Mesh.SurfaceIndexes.Push((int)i);
					s.numElements += 3;
				}
			}
		}
		range.Count = Mesh.Elements.Size() - range.Start;

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
			auto& vertex = Mesh.Vertices[surf->startVertIndex + i];
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
			if (Mesh.Vertices[i].fPos()[j] < low[j])
			{
				low[j] = Mesh.Vertices[i].fPos()[j];
			}
			if (Mesh.Vertices[i].fPos()[j] > hi[j])
			{
				hi[j] = Mesh.Vertices[i].fPos()[j];
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
		FVector3 tDelta = Mesh.Vertices[surface.startVertIndex + i].fPos() - surface.translateWorldToLocal;

		Mesh.Vertices[surface.startVertIndex + i].lu = (tDelta | surface.projLocalToU);
		Mesh.Vertices[surface.startVertIndex + i].lv = (tDelta | surface.projLocalToV);
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
