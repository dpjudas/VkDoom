#include "hw_lightprobe.h"
#include "v_video.h"

void LightProbeIncrementalBuilder::Step(const TArray<LightProbe>& probes, std::function<void(int probeIndex, const LightProbe& probe)> renderScene)
{
	if (probes.size() == 0)
		return;

	if (lastIndex >= probes.size())
	{
		lastIndex = 0;
	}

	if (cubemapsAllocated != probes.size())
	{
		int newSegments = probes.size();
		int lastSegments = cubemapsAllocated;

		cubemapsAllocated = probes.size();

		lastIndex = 0;
		collected = 0;
		iterations = 0;

		screen->ResetLightProbes();
		return;
	}

	if (iterations >= 5)
		return; // We are done baking

	renderScene(lastIndex, probes[lastIndex]);
	lastIndex++;
	collected++;

	if (lastIndex >= probes.size())
	{
		if (collected == probes.size())
		{
			screen->EndLightProbePass();
		}
		collected = 0;
		iterations++;
	}
}

void LightProbeIncrementalBuilder::Full(const TArray<LightProbe>& probes, std::function<void(int probeIndex, const LightProbe& probe)> renderScene)
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
		Step(probes, renderScene);
	}
}
