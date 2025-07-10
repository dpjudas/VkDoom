#include "c_dispatch.h"
#include "g_levellocals.h"

static void DumpLightProbeTargets()
{
	for (int i = 0, size = level.sectors.size(); i < size; ++i)
	{
		const auto& sector = level.sectors[i];
		Printf("Sector %d = %d\n", i, sector.lightProbe.index);
	}

	for (int i = 0, size = level.sides.size(); i < size; ++i)
	{
		const auto& side = level.sides[i];
		Printf("Side %d = %d\n", i, side.lightProbe.index);
	}
}

static void DumpLightProbes()
{
	for (int i = 0, size = level.lightProbes.size(); i < size; ++i)
	{
		const auto& probe = level.lightProbes[i];
		Printf("Probe %d: (%.1f, %.1f, %.1f)\n", i, probe.position.X, probe.position.Y, probe.position.Z);
	}
}

static void AddLightProbe(FVector3 position)
{
	LightProbe probe;
	probe.position = position;
	probe.index = static_cast<int>(level.lightProbes.size());
	level.lightProbes.Push(probe);
}

CCMD(dumplightprobes)
{
	DumpLightProbes();
}

CCMD(dumplightprobetargets)
{
	DumpLightProbeTargets();
}

CCMD(addlightprobe)
{
	const auto pos = FVector3(players[0].mo->Pos().X, players[0].mo->Pos().Y, players[0].viewz);
	AddLightProbe(pos);
	level.RecalculateLightProbeTargets();

	Printf("Spawned probe at %.1f, %.1f, %.1f\n", pos.X, pos.Y, pos.Z);
}

CCMD(autoaddlightprobes)
{
	int probes = 0;
	for (int i = 0, size = level.sectors.size(); i < size; ++i)
	{
		auto& sector = level.sectors[i];
		const auto origin = FVector3(sector.centerspot.X, sector.centerspot.Y, float(sector.floorplane.ZatPoint(sector.centerspot) + sector.ceilingplane.ZatPoint(sector.centerspot) / 2.f));

		/*if (level.lightProbes.size() > 0)
		{
			auto pos = FindClosestProbe(origin);
			if ((level.lightProbes[pos].position - origin).LengthSquared() < 256 * 256)
			{
				continue;
			}
		}*/

		probes++;
		AddLightProbe(origin);
	}

	level.RecalculateLightProbeTargets();

	Printf("Spawned %d probes\n", probes);
}

CCMD(setlightlevel)
{
	if (argv.argc() < 2)
	{
		Printf("Usage: setlightlevel <lightlevel>\n");
		return;
	}

	int light = std::atoi(argv[1]);

	for (int i = 0, size = level.sectors.size(); i < size; ++i)
	{
		auto& sector = level.sectors[i];
		sector.SetLightLevel(light);
	}
}