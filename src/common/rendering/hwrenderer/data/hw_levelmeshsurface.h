
#pragma once

#include "tarray.h"
#include "vectors.h"
#include "bounds.h"
#include "textureid.h"
#include "textures.h"

class LevelSubmesh;
class FGameTexture;
struct LevelMeshSurface;

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

	BBox Bounds;
	FVector4 Plane = FVector4(0.0f, 0.0f, 1.0f, 0.0f);
	int LightmapTileIndex = -1;

	bool AlwaysUpdate = false;

	FGameTexture* Texture = nullptr;
	float Alpha = 1.0;
	
	bool IsSky = false;
	int PortalIndex = 0;
	int SectorGroup = 0;

	// Light list location in the lightmapper GPU buffers
	struct
	{
		int Pos = -1;
		int Count = 0;
		int ResetCounter = -1;
	} LightList;
};
