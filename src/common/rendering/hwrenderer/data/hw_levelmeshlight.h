
#pragma once

#include "vectors.h"

class LevelMeshLight
{
public:
	FVector3 Origin;
	FVector3 RelativeOrigin;
	float Radius;
	float Intensity;
	float InnerAngleCos;
	float OuterAngleCos;
	FVector3 SpotDir;
	FVector3 Color;
	int SectorGroup;
};
