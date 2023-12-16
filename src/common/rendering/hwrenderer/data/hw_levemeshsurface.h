
#pragma once

#include "tarray.h"
#include "vectors.h"
#include "bounds.h"
#include "textureid.h"
#include "textures.h"

class LevelSubmesh;
class FGameTexture;

struct LevelMeshSurface
{
	LevelSubmesh* Submesh = nullptr;

	struct
	{
		unsigned int StartVertIndex = 0;
		int NumVerts = 0;
		unsigned int StartElementIndex = 0;
		unsigned int NumElements = 0;
	} MeshLocation;

	FVector4 Plane = FVector4(0.0f, 0.0f, 1.0f, 0.0f);

	// Surface location in lightmap texture
	struct
	{
		int X = 0;
		int Y = 0;
		int Width = 0;
		int Height = 0;
		int ArrayIndex = 0;
		uint32_t Area() const { return Width * Height; }
	} AtlasTile;

	// True if the surface needs to be rendered into the lightmap texture before it can be used
	bool NeedsUpdate = true;
	bool AlwaysUpdate = false;

	FGameTexture* Texture = nullptr;
	float Alpha = 1.0;
	
	bool IsSky = false;
	int PortalIndex = 0;
	int SectorGroup = 0;

	BBox Bounds;
	uint16_t SampleDimension = 0;

	// Calculate world coordinates to UV coordinates
	struct
	{
		FVector3 TranslateWorldToLocal = { 0.0f, 0.0f, 0.0f };
		FVector3 ProjLocalToU = { 0.0f, 0.0f, 0.0f };
		FVector3 ProjLocalToV = { 0.0f, 0.0f, 0.0f };
	} TileTransform;

	// Surfaces that are visible within the lightmap tile
	TArray<LevelMeshSurface*> TileSurfaces;

	// Light list location in the lightmapper GPU buffers
	struct
	{
		int Pos = -1;
		int Count = 0;
		int ResetCounter = -1;
	} LightList;
};

struct LevelMeshSmoothingGroup
{
	FVector4 plane = FVector4(0, 0, 1, 0);
	int sectorGroup = 0;
	std::vector<LevelMeshSurface*> surfaces;
};
