
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
		float u = (localPos | Transform.ProjLocalToU) / AtlasLocation.Width;
		float v = (localPos | Transform.ProjLocalToV) / AtlasLocation.Height;
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
		// These calculations align the tile so that there's a one texel border around the actual surface in the tile.
		// 
		// This removes sampling artifacts as a linear sampler reads from a 2x2 area.
		// The tile is also aligned to the grid to keep aliasing artifacts consistent.

		FVector3 uvMin;
		uvMin.X = std::floor(Bounds.min.X / SampleDimension) - 1.0f;
		uvMin.Y = std::floor(Bounds.min.Y / SampleDimension) - 1.0f;
		uvMin.Z = std::floor(Bounds.min.Z / SampleDimension) - 1.0f;

		FVector3 uvMax;
		uvMax.X = std::floor(Bounds.max.X / SampleDimension) + 2.0f;
		uvMax.Y = std::floor(Bounds.max.Y / SampleDimension) + 2.0f;
		uvMax.Z = std::floor(Bounds.max.Z / SampleDimension) + 2.0f;

		FVector3 tCoords[2] = { FVector3(0.0f, 0.0f, 0.0f), FVector3(0.0f, 0.0f, 0.0f) };
		int width, height;
		switch (BestAxis(Plane))
		{
		default:
		case AXIS_YZ:
			width = (int)(uvMax.Y - uvMin.Y);
			height = (int)(uvMax.Z - uvMin.Z);
			tCoords[0].Y = 1.0f / SampleDimension;
			tCoords[1].Z = 1.0f / SampleDimension;
			break;

		case AXIS_XZ:
			width = (int)(uvMax.X - uvMin.X);
			height = (int)(uvMax.Z - uvMin.Z);
			tCoords[0].X = 1.0f / SampleDimension;
			tCoords[1].Z = 1.0f / SampleDimension;
			break;

		case AXIS_XY:
			width = (int)(uvMax.X - uvMin.X);
			height = (int)(uvMax.Y - uvMin.Y);
			tCoords[0].X = 1.0f / SampleDimension;
			tCoords[1].Y = 1.0f / SampleDimension;
			break;
		}

		textureSize -= 6; // Lightmapper needs some padding when baking

		// Tile can never be bigger than the texture.
		if (width > textureSize)
		{
			tCoords[0] *= textureSize / (float)width;
			width = textureSize;
		}
		if (height > textureSize)
		{
			tCoords[1] *= textureSize / (float)height;
			height = textureSize;
		}

		Transform.TranslateWorldToLocal.X = uvMin.X * SampleDimension;
		Transform.TranslateWorldToLocal.Y = uvMin.Y * SampleDimension;
		Transform.TranslateWorldToLocal.Z = uvMin.Z * SampleDimension;

		Transform.ProjLocalToU = tCoords[0];
		Transform.ProjLocalToV = tCoords[1];

		AtlasLocation.Width = width;
		AtlasLocation.Height = height;
	}
};
