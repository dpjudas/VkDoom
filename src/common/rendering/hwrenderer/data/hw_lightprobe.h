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
	int collected = 0;

	int cubemapsAllocated = 0;

	TArray<uint16_t> irradianceMaps;
	TArray<uint16_t> prefilterMaps;

	size_t irradianceBytes;
	size_t prefilterBytes;
public:
	LightProbeIncrementalBuilder(size_t irradianceTexels, size_t prefilterTexels, size_t irradianceChannels, size_t prefilterChannels) :
		irradianceBytes(irradianceTexels * irradianceChannels),
		prefilterBytes(prefilterTexels * prefilterChannels)
	{
	}

	void Step(const TArray<LightProbe>& probes, std::function<void(const LightProbe&, TArrayView<uint16_t>&, TArrayView<uint16_t>&)> renderScene, std::function<void(const TArray<uint16_t>&, const TArray<uint16_t>&)> uploadEnv);
	void Full(const TArray<LightProbe>& probes, std::function<void(const LightProbe&, TArrayView<uint16_t>&, TArrayView<uint16_t>&)> renderScene, std::function<void(const TArray<uint16_t>&, const TArray<uint16_t>&)> uploadEnv);

	auto GetStep() const { return lastIndex; }
	auto GetBufferSize() const { return irradianceMaps.size() && prefilterMaps.size(); }
};