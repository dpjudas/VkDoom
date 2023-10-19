#pragma once

#include "hw_drawstructs.h"

class HWPortalWall
{
public:
	static void Process(HWWallDispatcher* di, FRenderState& state, seg_t* seg, sector_t* frontsector, sector_t* backsector);

private:
	static bool IsPortal(HWWallDispatcher* di, FRenderState& state, seg_t* seg, sector_t* frontsector, sector_t* backsector);
};
