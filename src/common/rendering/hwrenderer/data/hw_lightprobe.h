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

class LightProbeIncrementalBuilder
{
public:
	void Step(const TArray<LightProbe>& probes, std::function<void(int probeIndex, const LightProbe& probe)> renderScene);
	void Full(const TArray<LightProbe>& probes, std::function<void(int probeIndex, const LightProbe& probe)> renderScene);

	auto GetStep() const { return lastIndex; }

private:
	int lastIndex = 0;
	int collected = 0;
	int cubemapsAllocated = 0;
	int iterations = 0;
};
