
#include "lightmapcmd.h"

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
	Printf("Baking LIGHTMAP lump!\n");
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
	Printf("Deleting LIGHTMAP lump!\n");
}

void LightmapDeleteCmdlet::OnPrintHelp()
{
	Printf(TEXTCOLOR_ORANGE "lightmap delete " TEXTCOLOR_CYAN "[map name]" TEXTCOLOR_NORMAL " - Deletes the LIGHTMAP lump\n");
}
