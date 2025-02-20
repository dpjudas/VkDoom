#pragma once

#include "vectors.h"

struct LightProbe
{
	FVector3 position;
	int index = 0;
};

struct LightProbeTarget
{
	int index = 0; // parameter for renderstate.SetLightProbe
};
