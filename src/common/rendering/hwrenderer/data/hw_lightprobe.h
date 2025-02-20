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
	int lastIndex = 0;

	int collected;
	
	TArray<uint16_t> irradianceMaps;
	TArray<uint16_t> prefilterMaps;

public:

	void Step(const TArray<LightProbe>& probes, std::function<void(const LightProbe&, TArray<uint16_t>&, TArray<uint16_t>&)> renderScene, std::function<void(TArray<uint16_t>&&, TArray<uint16_t>&&)> uploadEnv);
	void Full(const TArray<LightProbe>& probes, std::function<void(const LightProbe&, TArray<uint16_t>&, TArray<uint16_t>&)> renderScene, std::function<void(TArray<uint16_t>&&, TArray<uint16_t>&&)> uploadEnv);

	auto GetStep() const { return lastIndex; }
};