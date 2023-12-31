
#pragma once

#include <cstring>
#include "vectors.h"
#include "common/utility/matrix.h"

struct LevelMeshPortal
{
	LevelMeshPortal() { transformation.loadIdentity(); }

	VSMatrix transformation;

	int sourceSectorGroup = 0;
	int targetSectorGroup = 0;

	inline FVector3 TransformPosition(const FVector3& pos) const
	{
		auto v = transformation * FVector4(pos, 1.0);
		return FVector3(v.X, v.Y, v.Z);
	}

	inline FVector3 TransformRotation(const FVector3& dir) const
	{
		auto v = transformation * FVector4(dir, 0.0);
		return FVector3(v.X, v.Y, v.Z);
	}

	// Checks only transformation
	inline bool IsInverseTransformationPortal(const LevelMeshPortal& portal) const
	{
		auto diff = portal.TransformPosition(TransformPosition(FVector3(0, 0, 0)));
		return abs(diff.X) < 0.001 && abs(diff.Y) < 0.001 && abs(diff.Z) < 0.001;
	}

	// Checks only transformation
	inline bool IsEqualTransformationPortal(const LevelMeshPortal& portal) const
	{
		auto diff = portal.TransformPosition(FVector3(0, 0, 0)) - TransformPosition(FVector3(0, 0, 0));
		return (abs(diff.X) < 0.001 && abs(diff.Y) < 0.001 && abs(diff.Z) < 0.001);
	}

	// Checks transformation, source and destiantion sector groups
	inline bool IsEqualPortal(const LevelMeshPortal& portal) const
	{
		return sourceSectorGroup == portal.sourceSectorGroup && targetSectorGroup == portal.targetSectorGroup && IsEqualTransformationPortal(portal);
	}

	// Checks transformation, source and destiantion sector groups
	inline bool IsInversePortal(const LevelMeshPortal& portal) const
	{
		return sourceSectorGroup == portal.targetSectorGroup && targetSectorGroup == portal.sourceSectorGroup && IsInverseTransformationPortal(portal);
	}
};

// for use with std::set to recursively go through portals and skip returning portals
struct RecursivePortalComparator
{
	bool operator()(const LevelMeshPortal& a, const LevelMeshPortal& b) const
	{
		return !a.IsInversePortal(b) && std::memcmp(&a.transformation, &b.transformation, sizeof(VSMatrix)) < 0;
	}
};

// for use with std::map to reject portals which have the same effect for light rays
struct IdenticalPortalComparator
{
	bool operator()(const LevelMeshPortal& a, const LevelMeshPortal& b) const
	{
		return !a.IsEqualPortal(b) && std::memcmp(&a.transformation, &b.transformation, sizeof(VSMatrix)) < 0;
	}
};
