
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
	TArray<LevelMeshSurface*> Surfaces;

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
		FVector3 localPos = vert - Transform.TranslateWorldToLocal;
		float u = (AtlasLocation.X + (localPos | Transform.ProjLocalToU)) / textureSize;
		float v = (AtlasLocation.Y + (localPos | Transform.ProjLocalToV)) / textureSize;
		return FVector2(u, v);
	}
};
