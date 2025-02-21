#include "hw_lightprobe.h"

void LightProbeIncrementalBuilder::Step(const TArray<LightProbe>& probes, std::function<void(const LightProbe&, TArrayView<uint16_t>&, TArrayView<uint16_t>&)> renderScene, std::function<void(const TArray<uint16_t>&, const TArray<uint16_t>&)> uploadEnv)
{
	if (lastIndex >= probes.size())
	{
		lastIndex = 0;

		if (!probes.size())
		{
			return;
		}
	}

	if (cubemapsAllocated != probes.size())
	{
		int newSegments = probes.size();
		int lastSegments = cubemapsAllocated;

		irradianceMaps.resize(probes.size() * irradianceBytes);
		prefilterMaps.resize(probes.size() * prefilterBytes);
		cubemapsAllocated = probes.size();

		lastIndex = 0;
		collected = 0;

		// needed because otherwise it somehow gets corrupted
		memset(irradianceMaps.Data(), 0, irradianceMaps.Size() * sizeof(uint16_t));
		memset(prefilterMaps.Data(), 0, prefilterMaps.Size() * sizeof(uint16_t));

		// workaround for lack of boundary checking in GPU
		uploadEnv(this->irradianceMaps, this->prefilterMaps);
		return;
	}

	auto irradianceBuffer = TArrayView<uint16_t>(irradianceMaps.data() + lastIndex * irradianceBytes, irradianceBytes);
	auto prefilterBuffer = TArrayView<uint16_t>(prefilterMaps.data() + lastIndex * prefilterBytes, prefilterBytes);

	renderScene(probes[lastIndex++], irradianceBuffer, prefilterBuffer);
	++collected;

	if (lastIndex >= probes.size())
	{
		if (collected == probes.size())
		{
			uploadEnv(this->irradianceMaps, this->prefilterMaps);
		}
		collected = 0;
	}
}

void LightProbeIncrementalBuilder::Full(const TArray<LightProbe>& probes, std::function<void(const LightProbe&, TArrayView<uint16_t>&, TArrayView<uint16_t>&)> renderScene, std::function<void(const TArray<uint16_t>&, const TArray<uint16_t>&)> uploadEnv)
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
