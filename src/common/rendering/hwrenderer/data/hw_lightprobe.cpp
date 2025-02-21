#include "hw_lightprobe.h"

void LightProbeIncrementalBuilder::Step(const TArray<LightProbe>& probes, std::function<void(const LightProbe&, TArray<uint16_t>&, TArray<uint16_t>&)> renderScene, std::function<void(TArray<uint16_t>&&, TArray<uint16_t>&&)> uploadEnv)
{
	if (lastIndex >= probes.size())
	{
		lastIndex = 0;

		if (!probes.size())
		{
			return;
		}
	}

	TArray<uint16_t> irradianceMap;
	TArray<uint16_t> prefilterMap;

	renderScene(probes[lastIndex++], irradianceMap, prefilterMap);
	++collected;

	this->irradianceMaps.Append(irradianceMap);
	this->prefilterMaps.Append(prefilterMap);

	if (lastIndex >= probes.size())
	{
		if (collected == probes.size())
		{
			uploadEnv(std::move(this->irradianceMaps), std::move(this->prefilterMaps));
		}
		this->irradianceMaps.Clear();
		this->prefilterMaps.Clear();
		collected = 0;
	}
}

void LightProbeIncrementalBuilder::Full(const TArray<LightProbe>& probes, std::function<void(const LightProbe&, TArray<uint16_t>&, TArray<uint16_t>&)> renderScene, std::function<void(TArray<uint16_t>&&, TArray<uint16_t>&&)> uploadEnv)
{
	if (lastIndex >= probes.size())
	{
		lastIndex = 0;

		if (!probes.size())
		{
			return;
		}
	}

	while (lastIndex < probes.size())
	{
		Step(probes, renderScene, uploadEnv);
	}
}
