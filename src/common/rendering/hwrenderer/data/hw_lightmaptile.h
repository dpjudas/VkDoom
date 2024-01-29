
#pragma once

#include "tarray.h"
#include "vectors.h"
#include "bounds.h"

struct LevelMeshSurface;

struct LightmapTileBinding
{
	uint32_t Type = 0;
	uint32_t TypeIndex = 0;
	uint32_t ControlSector = 0xffffffff;

	bool operator<(const LightmapTileBinding& other) const
	{
		if (TypeIndex != other.TypeIndex) return TypeIndex < other.TypeIndex;
		if (ControlSector != other.ControlSector) return ControlSector < other.ControlSector;
		return Type < other.Type;
	}
};

struct LightmapTile
{
	// Surface location in lightmap texture
	struct
	{
		int X = 0;
		int Y = 0;
		int Width = 0;
		int Height = 0;
		int ArrayIndex = 0;
		uint32_t Area() const { return Width * Height; }
	} AtlasLocation;

	// Calculate world coordinates to UV coordinates
	struct
	{
		FVector3 TranslateWorldToLocal = { 0.0f, 0.0f, 0.0f };
		FVector3 ProjLocalToU = { 0.0f, 0.0f, 0.0f };
		FVector3 ProjLocalToV = { 0.0f, 0.0f, 0.0f };
	} Transform;

	LightmapTileBinding Binding;

	// Surfaces that are visible within the lightmap tile
	TArray<int> Surfaces;

	BBox Bounds;
	uint16_t SampleDimension = 0;
	FVector4 Plane = FVector4(0.0f, 0.0f, 1.0f, 0.0f);

	// True if the tile needs to be rendered into the lightmap texture before it can be used
	bool NeedsUpdate = true;

	FVector2 ToUV(const FVector3& vert) const
	{
		FVector3 localPos = vert - Transform.TranslateWorldToLocal;
		float u = (1.0f + (localPos | Transform.ProjLocalToU)) / (AtlasLocation.Width + 2);
		float v = (1.0f + (localPos | Transform.ProjLocalToV)) / (AtlasLocation.Height + 2);
		return FVector2(u, v);
	}

	FVector2 ToUV(const FVector3& vert, float textureSize) const
	{
		// Clamp in case the wall moved outside the tile (happens if a lift moves with a static lightmap on it)
		FVector3 localPos = vert - Transform.TranslateWorldToLocal;
		float u = std::max(std::min(localPos | Transform.ProjLocalToU, (float)AtlasLocation.Width), 0.0f);
		float v = std::max(std::min(localPos | Transform.ProjLocalToV, (float)AtlasLocation.Height), 0.0f);
		u = (AtlasLocation.X + u) / textureSize;
		v = (AtlasLocation.Y + v) / textureSize;
		return FVector2(u, v);
	}

	enum PlaneAxis
	{
		AXIS_YZ = 0,
		AXIS_XZ,
		AXIS_XY
	};

	static PlaneAxis BestAxis(const FVector4& p)
	{
		float na = fabs(float(p.X));
		float nb = fabs(float(p.Y));
		float nc = fabs(float(p.Z));

		// figure out what axis the plane lies on
		if (na >= nb && na >= nc)
		{
			return AXIS_YZ;
		}
		else if (nb >= na && nb >= nc)
		{
			return AXIS_XZ;
		}

		return AXIS_XY;
	}

	void SetupTileTransform(int textureSize)
	{
		BBox bounds = Bounds;

		// round off dimensions
		FVector3 roundedSize;
		for (int i = 0; i < 3; i++)
		{
			bounds.min[i] = SampleDimension * (floor(bounds.min[i] / SampleDimension) - 1);
			bounds.max[i] = SampleDimension * (ceil(bounds.max[i] / SampleDimension) + 1);
			roundedSize[i] = (bounds.max[i] - bounds.min[i]) / SampleDimension;
		}

		FVector3 tCoords[2] = { FVector3(0.0f, 0.0f, 0.0f), FVector3(0.0f, 0.0f, 0.0f) };

		PlaneAxis axis = BestAxis(Plane);

		int width;
		int height;
		switch (axis)
		{
		default:
		case AXIS_YZ:
			width = (int)roundedSize.Y;
			height = (int)roundedSize.Z;
			tCoords[0].Y = 1.0f / SampleDimension;
			tCoords[1].Z = 1.0f / SampleDimension;
			break;

		case AXIS_XZ:
			width = (int)roundedSize.X;
			height = (int)roundedSize.Z;
			tCoords[0].X = 1.0f / SampleDimension;
			tCoords[1].Z = 1.0f / SampleDimension;
			break;

		case AXIS_XY:
			width = (int)roundedSize.X;
			height = (int)roundedSize.Y;
			tCoords[0].X = 1.0f / SampleDimension;
			tCoords[1].Y = 1.0f / SampleDimension;
			break;
		}

		// clamp width
		if (width > textureSize - 2)
		{
			tCoords[0] *= ((float)(textureSize - 2) / (float)width);
			width = (textureSize - 2);
		}

		// clamp height
		if (height > textureSize - 2)
		{
			tCoords[1] *= ((float)(textureSize - 2) / (float)height);
			height = (textureSize - 2);
		}

		Transform.TranslateWorldToLocal = bounds.min;
		Transform.ProjLocalToU = tCoords[0];
		Transform.ProjLocalToV = tCoords[1];

		AtlasLocation.Width = width;
		AtlasLocation.Height = height;
	}
};
