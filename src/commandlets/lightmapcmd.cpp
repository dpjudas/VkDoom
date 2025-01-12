
#include "lightmapcmd.h"
#include "g_levellocals.h"
#include "d_event.h"
#include "v_video.h"

void G_SetMap(const char* mapname, int mode);
void D_SingleTick();

LightmapCmdletGroup::LightmapCmdletGroup()
{
	SetLongFormName("lightmap");
	SetShortDescription("Lightmapper commands");

	AddCommand<LightmapBuildCmdlet>();
	AddCommand<LightmapDeleteCmdlet>();
}

/////////////////////////////////////////////////////////////////////////////

LightmapBuildCmdlet::LightmapBuildCmdlet()
{
	SetLongFormName("build");
	SetShortDescription("Build lightmap lump");
}

void LightmapBuildCmdlet::OnCommand(FArgs args)
{
	RunInGame([&]() {

		FString mapname;
		if (args.NumArgs() > 0 && args.GetArg(0)[0] != '-')
			mapname = args.GetArg(0);
		else
			mapname = "map01";

		G_SetMap(mapname.GetChars(), 0);
		for (int i = 0; i < 100; i++)
		{
			D_SingleTick();
			if (gameaction == ga_nothing)
				break;
		}

		if (!level.levelMesh)
		{
			Printf("No level mesh. Perhaps your level has no lightmap loaded?\n");
			return;
		}

		if (!level.lightmaps)
		{
			Printf("Lightmap is not enabled in this level.\n");
		}

		Printf("Baking lightmaps. Please wait...\n");

		uint32_t atlasPixelCount = level.levelMesh->AtlasPixelCount();
		auto stats = level.levelMesh->GatherTilePixelStats();

		Printf("Surfaces: %u (awaiting updates: %u static, %u dynamic)\n", stats.tiles.total, stats.tiles.dirty, stats.tiles.dirtyDynamic);
		Printf("Surface pixel area to update: %u static, %u dynamic\n", stats.pixels.dirty, stats.pixels.dirtyDynamic);
		Printf("Surface pixel area: %u\nAtlas pixel area: %u\n", stats.pixels.total, atlasPixelCount);
		Printf("Atlas efficiency: %.4f%%\n", float(stats.pixels.total) / float(atlasPixelCount) * 100.0f);

		Printf("Baking lightmap. Please wait...\n");

		TArray<LightmapTile*> tiles;

		while (stats.tiles.dirty > 0)
		{
			tiles.Clear();
			for (auto& e : level.levelMesh->Lightmap.Tiles)
			{
				if (e.NeedsUpdate && e.AlwaysUpdate == 0)
				{
					tiles.Push(&e);
					if (tiles.Size() == 1001)
						break;
				}
			}

			if (tiles.Size() == 0)
				break;

			screen->BeginFrame();
			screen->UpdateLightmaps(tiles);
			screen->Update();
		}

		Printf("Finished baking map.\n");
		level.levelMesh->SaveLightmapLump(level);

		Printf("Lightmap build complete.\n");
	});
}

void LightmapBuildCmdlet::OnPrintHelp()
{
	Printf(TEXTCOLOR_ORANGE "lightmap build " TEXTCOLOR_CYAN "[map name]" TEXTCOLOR_NORMAL " - Bakes all the lightmap lights and stores the result in a LIGHTMAP lump\n");
}

/////////////////////////////////////////////////////////////////////////////

LightmapDeleteCmdlet::LightmapDeleteCmdlet()
{
	SetLongFormName("delete");
	SetShortDescription("Delete lightmap lump");
}

void LightmapDeleteCmdlet::OnCommand(FArgs args)
{
	// Pretty lame that we have to load the map here, but cba to figure out how to deal with map loading outside the engine...

	RunInGame([&]() {
		FString mapname;
		if (args.NumArgs() > 0 && args.GetArg(0)[0] != '-')
			mapname = args.GetArg(0);
		else
			mapname = "map01";

		G_SetMap(mapname.GetChars(), 0);
		for (int i = 0; i < 100; i++)
		{
			D_SingleTick();
			if (gameaction == ga_nothing)
				break;
		}

		if (!level.levelMesh)
		{
			Printf("No level mesh. Perhaps your level has no lightmap loaded?\n");
			return;
		}

		if (!level.lightmaps)
		{
			Printf("Lightmap is not enabled in this level.\n");
		}

		level.levelMesh->DeleteLightmapLump(level);
		
	});
}

void LightmapDeleteCmdlet::OnPrintHelp()
{
	Printf(TEXTCOLOR_ORANGE "lightmap delete " TEXTCOLOR_CYAN "[map name]" TEXTCOLOR_NORMAL " - Deletes the LIGHTMAP lump\n");
}
