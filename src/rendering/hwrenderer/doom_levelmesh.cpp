
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
#include "common/rendering/vulkan/accelstructs/vk_lightmap.h"
#include "common/rendering/vulkan/accelstructs/halffloat.h"

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
		if (!surface.NeedsUpdate)
			++count;
		surface.NeedsUpdate = true;
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
	Printf("    Needs update?: %d\n", surface->NeedsUpdate);
	Printf("    Always update?: %d\n", surface->AlwaysUpdate);
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
