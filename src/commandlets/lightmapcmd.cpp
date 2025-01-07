
#include "lightmapcmd.h"
#include "g_levellocals.h"
#include "d_event.h"

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

		Printf("Baking LIGHTMAP lump!\n");
		Printf("Current map is %s", level.LevelName.GetChars());
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
	RunInGame([]() {
		Printf("Deleting LIGHTMAP lump!\n");
	});
}

void LightmapDeleteCmdlet::OnPrintHelp()
{
	Printf(TEXTCOLOR_ORANGE "lightmap delete " TEXTCOLOR_CYAN "[map name]" TEXTCOLOR_NORMAL " - Deletes the LIGHTMAP lump\n");
}
