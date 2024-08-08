// level mesh helper
// this is a minimal include to keep the other source files a little bit leaner

struct UpdateLevelMesh
{
	void SectorChanged(void *sector)
	{
	}

	void SideChanged(void *side)
	{
	}
};

UpdateLevelMesh* LevelMeshUpdater;
