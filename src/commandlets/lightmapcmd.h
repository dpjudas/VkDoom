
#pragma once

#include "commandlet.h"

class LightmapCmdletGroup : public CommandletGroup
{
public:
	LightmapCmdletGroup();
};

class LightmapBuildCmdlet : public Commandlet
{
public:
	LightmapBuildCmdlet();
	void OnCommand(FArgs args) override;
	void OnPrintHelp() override;
};

class LightmapDeleteCmdlet : public Commandlet
{
public:
	LightmapDeleteCmdlet();
	void OnCommand(FArgs args) override;
	void OnPrintHelp() override;
};
