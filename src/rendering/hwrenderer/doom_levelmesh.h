
#pragma once

#include "hw_levelmesh.h"
#include "doom_levelsubmesh.h"

class DoomLevelMesh : public LevelMesh
{
public:
	DoomLevelMesh(FLevelLocals &doomMap);

	int AddSurfaceLights(const LevelMeshSurface* surface, LevelMeshLight* list, int listMaxSize) override;

	void BeginFrame(FLevelLocals& doomMap);
	bool TraceSky(const FVector3& start, FVector3 direction, float dist);
	void PackLightmapAtlas();
	void DumpMesh(const FString& objFilename, const FString& mtlFilename) const;
};
