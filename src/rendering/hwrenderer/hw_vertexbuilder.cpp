// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2015-2018 Christoph Oelckers
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//



#include "g_levellocals.h"
#include "hw_vertexbuilder.h"
#include "hw_renderstate.h"
#include "flatvertices.h"
#include "earcut.hpp"
#include "v_video.h"

TArray<FFlatVertex> sector_vertices;
TArray<uint32_t> sector_indexes;

//=============================================================================
//
// Creates vertex meshes for sector planes
//
//=============================================================================

//=============================================================================
//
//
//
//=============================================================================

static void CreateVerticesForSubsector(subsector_t *sub, VertexContainer &gen, int qualifier)
{
	if (sub->numlines < 3) return;
	
	uint32_t startindex = gen.indices.Size();
	
	if ((sub->flags & SSECF_HOLE) && sub->numlines > 3)
	{
		// Hole filling "subsectors" are not necessarily convex so they require real triangulation.
		// These things are extremely rare so performance is secondary here.
		
		using Point = std::pair<double, double>;
		std::vector<std::vector<Point>> polygon;
		std::vector<Point> *curPoly;

		polygon.resize(1);
		curPoly = &polygon.back();
		curPoly->resize(sub->numlines);

		for (unsigned i = 0; i < sub->numlines; i++)
		{
			(*curPoly)[i] = { sub->firstline[i].v1->fX(), sub->firstline[i].v1->fY() };
		}
		auto indices = mapbox::earcut(polygon);
		for (auto vti : indices)
		{
			gen.AddIndexForVertex(sub->firstline[vti].v1, qualifier);
		}
	}
	else
	{
		int firstndx = gen.AddVertex(sub->firstline[0].v1, qualifier);
		int secondndx = gen.AddVertex(sub->firstline[1].v1, qualifier);
		for (unsigned int k = 2; k < sub->numlines; k++)
		{
			gen.AddIndex(firstndx);
			gen.AddIndex(secondndx);
			auto ndx = gen.AddVertex(sub->firstline[k].v1, qualifier);
			gen.AddIndex(ndx);
			secondndx = ndx;
		}
	}
}

//=============================================================================
//
//
//
//=============================================================================

static void TriangulateSection(FSection &sect, VertexContainer &gen, int qualifier)
{
	if (sect.segments.Size() < 3) return;
	
	// todo
}

//=============================================================================
//
//
//
//=============================================================================


static void CreateVerticesForSection(FSection &section, VertexContainer &gen, bool useSubsectors)
{
	section.vertexindex = gen.indices.Size();

	if (useSubsectors)
	{
		for (auto sub : section.subsectors)
		{
			CreateVerticesForSubsector(sub, gen, -1);
		}
	}
	else
	{
		TriangulateSection(section, gen, -1);
	}
	section.vertexcount = gen.indices.Size() - section.vertexindex;
}

//==========================================================================
//
// Creates the vertices for one plane in one subsector
//
//==========================================================================

static void CreateVerticesForSector(sector_t *sec, VertexContainer &gen)
{
	auto sections = sec->Level->sections.SectionsForSector(sec);
	for (auto &section :sections)
	{
		CreateVerticesForSection( section, gen, true);
	}
}


TArray<VertexContainer> BuildVertices(TArray<sector_t> &sectors)
{
	TArray<VertexContainer> verticesPerSector(sectors.Size(), true);
	for (unsigned i=0; i< sectors.Size(); i++)
	{
		CreateVerticesForSector(&sectors[i], verticesPerSector[i]);
	}
	return verticesPerSector;
}

//==========================================================================
//
// Creates the vertices for one plane in one subsector
//
//==========================================================================

//==========================================================================
//
// Find a 3D floor
//
//==========================================================================

static F3DFloor *Find3DFloor(sector_t* target, sector_t* model, int &ffloorIndex)
{
	for (unsigned i = 0; i < target->e->XFloor.ffloors.Size(); i++)
	{
		F3DFloor* ffloor = target->e->XFloor.ffloors[i];
		if (ffloor->model == model && !(ffloor->flags & FF_THISINSIDE))
		{
			ffloorIndex = i;
			return ffloor;
		}
	}
	ffloorIndex = -1;
	return NULL;
}

//==========================================================================
//
// Initialize a single vertex
//
//==========================================================================

static void SetFlatVertex(FFlatVertex& ffv, vertex_t* vt, const secplane_t& plane)
{
	ffv.x = (float)vt->fX();
	ffv.y = (float)vt->fY();
	ffv.z = (float)plane.ZatPoint(vt);
	ffv.u = (float)vt->fX() / 64.f;
	ffv.v = -(float)vt->fY() / 64.f;
	ffv.lindex = -1.0f;
}

static void SetFlatVertex(FFlatVertex& ffv, vertex_t* vt, const secplane_t& plane, float llu, float llv, float llindex)
{
	ffv.x = (float)vt->fX();
	ffv.y = (float)vt->fY();
	ffv.z = (float)plane.ZatPoint(vt);
	ffv.u = (float)vt->fX() / 64.f;
	ffv.v = -(float)vt->fY() / 64.f;
	ffv.lu = llu;
	ffv.lv = llv;
	ffv.lindex = llindex;
}

//==========================================================================
//
// Creates the vertices for one plane in one subsector w/lightmap support.
// Sectors with lightmaps cannot share subsector vertices.
//
//==========================================================================

static int CreateIndexedSectorVerticesLM(sector_t* sec, const secplane_t& plane, int floor, int h, int lightmapIndex)
{
	int i, pos;
	float diff;

	auto& ibo_data = sector_indexes;

	int rt = ibo_data.Size();
	if (sec->transdoor && floor) diff = -1.f;
	else diff = 0.f;

	// Allocate space
	for (i = 0, pos = 0; i < sec->subsectorcount; i++)
	{
		pos += sec->subsectors[i]->numlines;
	}

	auto& vbo_shadowdata = sector_vertices;
	int vi = vbo_shadowdata.Reserve(pos);
	int idx = ibo_data.Reserve((pos - 2 * sec->subsectorcount) * 3);

	// Create the actual vertices.
	auto sections = sec->Level->sections.SectionsForSector(sec);
	pos = 0;
	for(auto& section : sections)
	{
		for(auto& sub : section.subsectors)
		{
			// vertices
			int lightmap = sub->LightmapTiles[h].Size() > lightmapIndex ? sub->LightmapTiles[h][lightmapIndex] : -1;
			if (lightmap >= 0) // tile may be missing if the subsector is degenerate triangle
			{
				const auto& tile = level.levelMesh->Lightmap.Tiles[lightmap];
				float textureSize = (float)level.levelMesh->Lightmap.TextureSize;
				float lindex = (float)tile.AtlasLocation.ArrayIndex;
				for (unsigned int j = 0, end = sub->numlines; j < end; j++)
				{
					vertex_t* vt = sub->firstline[j].v1;
					FVector2 luv = tile.ToUV(FVector3((float)vt->fX(), (float)vt->fY(), (float)plane.ZatPoint(vt)), textureSize);
					SetFlatVertex(vbo_shadowdata[vi + pos], vt, plane, luv.X, luv.Y, lindex);
					vbo_shadowdata[vi + pos].z += diff;
					pos++;
				}
			}
			else
			{
				for (unsigned int j = 0; j < sub->numlines; j++)
				{
					SetFlatVertex(vbo_shadowdata[vi + pos], sub->firstline[j].v1, plane);
					vbo_shadowdata[vi + pos].z += diff;
					pos++;
				}
			}
		}
	}

	// Create the indices for the subsectors
	pos = 0;
	for (auto& section : sections)
	{
		for (auto& sub : section.subsectors)
		{
			int firstndx = vi + pos;
			for (unsigned int k = 2; k < sub->numlines; k++)
			{
				ibo_data[idx++] = firstndx;
				ibo_data[idx++] = firstndx + k - 1;
				ibo_data[idx++] = firstndx + k;
			}
			pos += sub->numlines;
		}
	}

	sec->ibocount = ibo_data.Size() - rt;
	return rt;
}

static int CreateIndexedSectorVertices(sector_t* sec, const secplane_t& plane, int floor, VertexContainer& verts, int h, int lightmapIndex)
{
	if (sec->HasLightmaps && lightmapIndex != -1)
		return CreateIndexedSectorVerticesLM(sec, plane, floor, h, lightmapIndex);

	auto& vbo_shadowdata = sector_vertices;
	unsigned vi = vbo_shadowdata.Reserve(verts.vertices.Size());
	float diff;

	// Create the actual vertices.
	if (sec->transdoor && floor) diff = -1.f;
	else diff = 0.f;
	for (unsigned i = 0; i < verts.vertices.Size(); i++)
	{
		SetFlatVertex(vbo_shadowdata[vi + i], verts.vertices[i].vertex, plane);
		vbo_shadowdata[vi + i].z += diff;
	}

	auto& ibo_data = sector_indexes;
	unsigned rt = ibo_data.Reserve(verts.indices.Size());
	for (unsigned i = 0; i < verts.indices.Size(); i++)
	{
		ibo_data[rt + i] = vi + verts.indices[i];
	}
	return (int)rt;
}

//==========================================================================
//
//
//
//==========================================================================

static int CreateIndexedVertices(int h, sector_t* sec, const secplane_t& plane, int floor, VertexContainers& verts)
{
	auto& vbo_shadowdata = sector_vertices;
	sec->vboindex[h] = vbo_shadowdata.Size();
	// First calculate the vertices for the sector itself
	sec->vboheight[h] = sec->GetPlaneTexZ(h);
	sec->ibocount = verts[sec->Index()].indices.Size();
	sec->iboindex[h] = CreateIndexedSectorVertices(sec, plane, floor, verts[sec->Index()], h, 0);

	// Next are all sectors using this one as heightsec
	TArray<sector_t*>& fakes = sec->e->FakeFloor.Sectors;
	for (unsigned g = 0; g < fakes.Size(); g++)
	{
		sector_t* fsec = fakes[g];
		fsec->iboindex[2 + h] = CreateIndexedSectorVertices(fsec, plane, false, verts[fsec->Index()], h, -1);
	}

	// and finally all attached 3D floors
	TArray<sector_t*>& xf = sec->e->XFloor.attached;
	for (unsigned g = 0; g < xf.Size(); g++)
	{
		sector_t* fsec = xf[g];
		int ffloorIndex;
		F3DFloor* ffloor = Find3DFloor(fsec, sec, ffloorIndex);

		if (ffloor != NULL && ffloor->flags & FF_RENDERPLANES)
		{
			bool dotop = (ffloor->top.model == sec) && (ffloor->top.isceiling == h);
			bool dobottom = (ffloor->bottom.model == sec) && (ffloor->bottom.isceiling == h);

			if (dotop || dobottom)
			{
				auto ndx = CreateIndexedSectorVertices(fsec, plane, false, verts[fsec->Index()], h, ffloorIndex + 1);
				if (dotop) ffloor->top.vindex = ndx;
				if (dobottom) ffloor->bottom.vindex = ndx;
			}
		}
	}
	sec->vbocount[h] = vbo_shadowdata.Size() - sec->vboindex[h];
	return sec->iboindex[h];
}


//==========================================================================
//
//
//
//==========================================================================

static void CreateIndexedFlatVertices(TArray<sector_t>& sectors)
{
	auto verts = BuildVertices(sectors);

	int i = 0;
	/*
	for (auto &vert : verts)
	{
		Printf(PRINT_LOG, "Sector %d\n", i);
		Printf(PRINT_LOG, "%d vertices, %d indices\n", vert.vertices.Size(), vert.indices.Size());
		int j = 0;
		for (auto &v : vert.vertices)
		{
			Printf(PRINT_LOG, "    %d: (%2.3f, %2.3f)\n", j++, v.vertex->fX(), v.vertex->fY());
		}
		for (unsigned i=0;i<vert.indices.Size();i+=3)
		{
			Printf(PRINT_LOG, "     %d, %d, %d\n", vert.indices[i], vert.indices[i + 1], vert.indices[i + 2]);
		}

		i++;
	}
	*/


	for (int h = sector_t::floor; h <= sector_t::ceiling; h++)
	{
		for (auto& sec : sectors)
		{
			CreateIndexedVertices(h, &sec, sec.GetSecPlane(h), h == sector_t::floor, verts);
		}
	}

	// We need to do a final check for Vavoom water and FF_FIX sectors.
	// No new vertices are needed here. The planes come from the actual sector
	for (auto& sec : sectors)
	{
		for (auto ff : sec.e->XFloor.ffloors)
		{
			if (ff->top.model == &sec)
			{
				ff->top.vindex = sec.iboindex[ff->top.isceiling];
			}
			if (ff->bottom.model == &sec)
			{
				ff->bottom.vindex = sec.iboindex[ff->top.isceiling];
			}
		}
	}
}

//==========================================================================
//
//
//
//==========================================================================

static void UpdatePlaneVertices(FRenderState& renderstate, sector_t* sec, int plane)
{
	int startvt = sec->vboindex[plane];
	int countvt = sec->vbocount[plane];
	secplane_t& splane = sec->GetSecPlane(plane);
	FFlatVertex* vt = &sector_vertices[startvt];
	for (int i = 0; i < countvt; i++, vt++)
	{
		vt->z = (float)splane.ZatPoint(vt->x, vt->y);
		if (plane == sector_t::floor && sec->transdoor) vt->z -= 1;
	}
	
	renderstate.UpdateShadowData(startvt, &sector_vertices[startvt], countvt);
}

//==========================================================================
//
//
//
//==========================================================================

static void UpdatePlaneLightmap(FRenderState& renderstate, sector_t* sec, int plane, int lightmapIndex)
{
	if (!sec->HasLightmaps)
		return;

	int startvt = sec->vboindex[plane];
	int countvt = sec->vbocount[plane];
	secplane_t& splane = sec->GetSecPlane(plane);

	auto sections = sec->Level->sections.SectionsForSector(sec);
	int pos = startvt;
	for (auto& section : sections)
	{
		for (auto& sub : section.subsectors)
		{
			// vertices
			int lightmap = sub->LightmapTiles[plane].Size() > lightmapIndex ? sub->LightmapTiles[plane][lightmapIndex] : -1;
			if (lightmap >= 0) // tile may be missing if the subsector is degenerate triangle
			{
				const auto& tile = level.levelMesh->Lightmap.Tiles[lightmap];
				float textureSize = (float)level.levelMesh->Lightmap.TextureSize;
				float lindex = (float)tile.AtlasLocation.ArrayIndex;
				for (unsigned int j = 0, end = sub->numlines; j < end; j++)
				{
					vertex_t* vt = sub->firstline[j].v1;
					FVector2 luv = tile.ToUV(FVector3((float)vt->fX(), (float)vt->fY(), (float)splane.ZatPoint(vt)), textureSize);
					sector_vertices[pos].lu = luv.X;
					sector_vertices[pos].lv = luv.Y;
					sector_vertices[pos].lindex = lightmapIndex;
					pos++;
				}
			}
		}
	}

	renderstate.UpdateShadowData(startvt, &sector_vertices[startvt], countvt);
}

//==========================================================================
//
//
//
//==========================================================================

static void CreateVertices(TArray<sector_t>& sectors)
{
	sector_vertices.Clear();
	CreateIndexedFlatVertices(sectors);
}

//==========================================================================
//
//
//
//==========================================================================

static void CheckPlanes(FRenderState& renderstate, sector_t* sector)
{
	if (sector->GetPlaneTexZ(sector_t::ceiling) != sector->vboheight[sector_t::ceiling])
	{
		UpdatePlaneVertices(renderstate, sector, sector_t::ceiling);
		sector->vboheight[sector_t::ceiling] = sector->GetPlaneTexZ(sector_t::ceiling);
	}
	if (sector->GetPlaneTexZ(sector_t::floor) != sector->vboheight[sector_t::floor])
	{
		UpdatePlaneVertices(renderstate, sector, sector_t::floor);
		sector->vboheight[sector_t::floor] = sector->GetPlaneTexZ(sector_t::floor);
	}
}

//==========================================================================
//
//
//
//==========================================================================

static void UpdateLightmapPlanes(FRenderState& renderstate, sector_t* sector)
{
	UpdatePlaneLightmap(renderstate, sector, sector_t::ceiling, 0);
	UpdatePlaneLightmap(renderstate, sector, sector_t::floor, 0);
}

//==========================================================================
//
// checks the validity of all planes attached to this sector
// and updates them if possible.
//
//==========================================================================

void CheckUpdate(FRenderState& renderstate, sector_t* sector)
{
	CheckPlanes(renderstate, sector);
	sector_t* hs = sector->GetHeightSec();
	if (hs != NULL) CheckPlanes(renderstate, hs);
	for (unsigned i = 0; i < sector->e->XFloor.ffloors.Size(); i++)
		CheckPlanes(renderstate, sector->e->XFloor.ffloors[i]->model);
}

//==========================================================================
//
// Update the lightmap UV coordinates for the sector
//
//==========================================================================

void UpdateVBOLightmap(FRenderState& renderstate, sector_t* sector)
{
	UpdatePlaneLightmap(renderstate, sector, sector_t::ceiling, 0);
	UpdatePlaneLightmap(renderstate, sector, sector_t::floor, 0);

	sector_t* hs = sector->GetHeightSec();
	if (hs != NULL)
	{
		UpdatePlaneLightmap(renderstate, hs, sector_t::ceiling, -1);
		UpdatePlaneLightmap(renderstate, hs, sector_t::floor, -1);
	}
	for (unsigned i = 0; i < sector->e->XFloor.ffloors.Size(); i++)
	{
		UpdatePlaneLightmap(renderstate, sector->e->XFloor.ffloors[i]->model, sector_t::ceiling, i + 1);
		UpdatePlaneLightmap(renderstate, sector->e->XFloor.ffloors[i]->model, sector_t::floor, i + 1);
	}
}

//==========================================================================
//
//
//
//==========================================================================

void CreateVBO(FRenderState& renderstate, TArray<sector_t>& sectors)
{
	sector_vertices.Clear();
	CreateVertices(sectors);
	renderstate.SetShadowData(sector_vertices, sector_indexes);
}
