#pragma once

#include "hw_renderstate.h"
#include "r_utility.h"
#include "hw_fakeflat.h"
#include "hw_drawcontext.h"
#include "hw_drawinfo.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

class Clipper;
class HWPortal;
struct HWDrawInfo;

class HWVisibleSet
{
public:
	void FindPVS(HWDrawInfo* di, int sliceIndex, int sliceCount);

	struct VisList
	{
		const TArray<int>& Get() const { return List; }

		void Clear()
		{
			for (int index : List)
				AddedToList[index] = false;
			List.Clear();
		}

		void Add(int index)
		{
			if (index >= (int)AddedToList.size())
			{
				int lastSize = AddedToList.size();
				AddedToList.resize(index + 1); // Beware! TArray does not clear on resize!
				for (int i = lastSize; i < index + 1; i++)
					AddedToList[i] = false;
			}

			if (!AddedToList[index])
			{
				AddedToList[index] = true;
				List.Push(index);
			}
		}

	private:
		TArray<int> List;
		TArray<bool> AddedToList;
	};

	VisList SeenSectors, SeenSides, SeenSubsectors, SeenHackedSubsectors, SeenSubsectorPortals;

private:
	void RenderBSP(void* node);
	void RenderBSPNode(void* node);
	void DoSubsector(subsector_t* sub);
	void UnclipSubsector(subsector_t* sub);
	void AddLines(subsector_t* sub, sector_t* sector);
	void AddLine(seg_t* seg, bool portalclip);
	void AddPolyobjs(subsector_t* sub);
	void RenderPolyBSPNode(void* node);
	void PolySubsector(subsector_t* sub);
	void AddHackedSubsector(subsector_t* sub);
	void AddSpecialPortalLines(subsector_t* sub, sector_t* sector, linebase_t* line);
	angle_t FrustumAngle();

	struct
	{
		TArray<int> sector;
		TArray<int> line;
		int current = 0;
	} validcount;

	HWDrawContext drawctx;
	FLevelLocals* Level = nullptr;
	FRenderViewpoint Viewpoint;
	CameraFrustum ClipFrustum;
	TArrayView<uint8_t> no_renderflags;
	BitArray* CurrentMapSections = nullptr;
	TArray<uint8_t> section_renderflags;
	TArray<uint8_t> ss_renderflags;
	area_t in_area = {};

	fixed_t viewx = 0, viewy = 0;	// since the nodes are still fixed point, keeping the view position  also fixed point for node traversal is faster.

	Clipper* mClipper = nullptr;
	HWPortal* mClipPortal = nullptr;

	sector_t* currentsector = nullptr;
	subsector_t* currentsubsector = nullptr;

	int rendered_lines = 0;
};

class HWVisibleSetThreads
{
public:
	HWVisibleSetThreads();
	~HWVisibleSetThreads();

	void FindPVS(HWDrawInfo* di);

private:
	void WorkerMain(int sliceIndex);

	int SliceCount = 1;
	TArray<HWVisibleSet> Slices;
	std::vector<std::thread> Threads;
	std::mutex Mutex;
	std::condition_variable WorkCondvar, DoneCondvar;
	std::vector<bool> WorkFlags;
	bool StopFlag = false;
	HWDrawInfo* DrawInfo = nullptr;
};
