
#include "hw_visibleset.h"
#include "hw_clipper.h"
#include "hw_drawinfo.h"
#include "hw_fakeflat.h"
#include "hw_drawstructs.h"
#include "hw_portal.h"
#include "g_levellocals.h"
#include "texturemanager.h"
#include "v_draw.h"
#include <mutex>

EXTERN_CVAR(Bool, gl_render_things);
EXTERN_CVAR(Bool, gl_render_walls);
EXTERN_CVAR(Bool, gl_render_flats);

// TArray resize doesn't initialize integers on resize
static void resizeIntArray(TArray<int>& arr, size_t newsize)
{
	size_t s = arr.size();
	if (s >= newsize)
	{
		arr.resize(s);
	}
	else
	{
		arr.resize(newsize);
		for (size_t i = s; i < newsize; i++)
			arr[i] = 0;
	}
}

angle_t HWVisibleSet::FrustumAngle()
{
	float tilt = fabs(Viewpoint.HWAngles.Pitch.Degrees());

	// If the pitch is larger than this you can look all around at a FOV of 90 degrees
	if (tilt > 46.0f) return 0xffffffff;

	// ok, this is a gross hack that barely works...
	// but at least it doesn't overestimate too much...
	double floatangle = 2.0 + (45.0 + ((tilt / 1.9))) * Viewpoint.FieldOfView.Degrees() * 48.0 / AspectMultiplier(r_viewwindow.WidescreenRatio) / 90.0;
	angle_t a1 = DAngle::fromDeg(floatangle).BAMs();
	if (a1 >= ANGLE_180) return 0xffffffff;
	return a1;
}

void HWVisibleSet::FindPVS(HWDrawInfo* di, int sliceIndex, int sliceCount)
{
	Level = di->Level;
	Viewpoint = di->Viewpoint;
	in_area = di->in_area;
	mClipPortal = di->mClipPortal;

	VSMatrix m = di->VPUniforms.mProjectionMatrix;
	m.multMatrix(di->VPUniforms.mViewMatrix);
	ClipFrustum.Set(m, Viewpoint.Pos);

	CurrentMapSections = &di->CurrentMapSections;
	no_renderflags = TArrayView<uint8_t>(di->no_renderflags.data(), di->no_renderflags.size());

	section_renderflags.Resize(Level->sections.allSections.Size());
	ss_renderflags.Resize(Level->subsectors.Size());
	memset(&section_renderflags[0], 0, Level->sections.allSections.Size() * sizeof(section_renderflags[0]));
	memset(&ss_renderflags[0], 0, Level->subsectors.Size() * sizeof(ss_renderflags[0]));

	drawctx.staticClipper.Clear();
	mClipper = &drawctx.staticClipper;
	mClipper->SetViewpoint(Viewpoint);

	const auto& vp = Viewpoint;
	angle_t a1 = FrustumAngle();

	// Find the range for our slice
	//uint64_t start = static_cast<uint64_t>(vp.Angles.Yaw.BAMs() + a1);
	uint64_t end = static_cast<uint64_t>(vp.Angles.Yaw.BAMs() - a1);
	uint64_t length = static_cast<uint64_t>(a1) * 2;
	uint32_t sliceend = static_cast<uint32_t>(end + length * sliceIndex / sliceCount);
	uint32_t slicestart = static_cast<uint32_t>(end + length * (sliceIndex + 1) / sliceCount);
	mClipper->SafeAddClipRangeRealAngles(slicestart, sliceend);

	drawctx.portalState.StartFrame();

	validcount.current++;
	resizeIntArray(validcount.sector, Level->sectors.size());
	resizeIntArray(validcount.line, Level->lines.size());

	hw_ClearFakeFlat(&drawctx);
	RenderBSP(Level->HeadNode());
}

void HWVisibleSet::RenderBSP(void* node)
{
	// Give the DrawInfo the viewpoint in fixed point because that's what the nodes are.
	viewx = FLOAT2FIXED(Viewpoint.Pos.X);
	viewy = FLOAT2FIXED(Viewpoint.Pos.Y);

	SeenSectors.Clear();
	SeenSides.Clear();
	SeenSubsectors.Clear();
	SeenHackedSubsectors.Clear();
	SeenSubsectorPortals.Clear();

	RenderBSPNode(node);
}

void HWVisibleSet::RenderBSPNode(void* node)
{
	if (Level->nodes.Size() == 0)
	{
		DoSubsector(&Level->subsectors[0]);
		return;
	}
	while (!((size_t)node & 1))  // Keep going until found a subsector
	{
		node_t* bsp = (node_t*)node;

		// Decide which side the view point is on.
		int side = R_PointOnSide(viewx, viewy, bsp);

		// Recursively divide front space (toward the viewer).
		RenderBSPNode(bsp->children[side]);

		// Possibly divide back space (away from the viewer).
		side ^= 1;

		// It is not necessary to use the slower precise version here
		if (!mClipper->CheckBox(bsp->bbox[side]))
		{
			if (!(no_renderflags[bsp->Index()] & SSRF_SEEN))
				return;
		}

		node = bsp->children[side];
	}
	DoSubsector((subsector_t*)((uint8_t*)node - 1));
}

void HWVisibleSet::DoSubsector(subsector_t* sub)
{
	sector_t* sector;
	sector_t* fakesector;

#ifdef _DEBUG
	if (sub->sector->sectornum == 931)
	{
		int a = 0;
	}
#endif

	sector = sub->sector;
	if (!sector) return;

	// If the mapsections differ this subsector can't possibly be visible from the current view point
	if (!(*CurrentMapSections)[sub->mapsection]) return;
	if (sub->flags & SSECF_POLYORG) return;	// never render polyobject origin subsectors because their vertices no longer are where one may expect.

	if (ss_renderflags[sub->Index()] & SSRF_SEEN)
	{
		// This means that we have reached a subsector in a portal that has been marked 'seen'
		// from the other side of the portal. This means we must clear the clipper for the
		// range this subsector spans before going on.
		UnclipSubsector(sub);
	}
	if (mClipper->IsBlocked()) return;	// if we are inside a stacked sector portal which hasn't unclipped anything yet.

	fakesector = hw_FakeFlat(&drawctx, sector, in_area, false);

	if (mClipPortal)
	{
		int clipres = mClipPortal->ClipSubsector(sub);
		if (clipres == PClip_InFront)
		{
			auto line = mClipPortal->ClipLine();
			// The subsector is out of range, but we still have to check lines that lie directly on the boundary and may expose their upper or lower parts.
			if (line)
				AddSpecialPortalLines(sub, fakesector, line);
			return;
		}
	}

	// [RH] Add particles
	if (gl_render_things && (sub->sprites.Size() > 0 || Level->ParticlesInSubsec[sub->Index()] != NO_PARTICLE))
	{
		SeenSubsectors.Add(sub->Index());
		//RenderParticles(sub, fakesector);
	}

	AddLines(sub, fakesector);

	// BSP is traversed by subsector.
	// A sector might have been split into several
	//	subsectors during BSP building.
	// Thus we check whether it was already added.
	if (validcount.sector[sector->Index()] != validcount.current)
	{
		// Well, now it will be done.
		validcount.sector[sector->Index()] = validcount.current;
		//sector->MoreFlags |= SECMF_DRAWN;

		if (gl_render_things && (sector->touching_renderthings || sector->sectorportal_thinglist))
		{
			SeenSubsectors.Add(sub->Index());
			//RenderThings(sub, fakesector);
		}
	}

	if (gl_render_flats)
	{
		// Subsectors with only 2 lines cannot have any area
		if (sub->numlines > 2 || (sub->hacked & 1))
		{
			// Exclude the case when it tries to render a sector with a heightsec
			// but undetermined heightsec state. This can only happen if the
			// subsector is obstructed but not excluded due to a large bounding box.
			// Due to the way a BSP works such a subsector can never be visible
			if (!sector->GetHeightSec() || in_area != area_default)
			{
				if (sector != sub->render_sector)
				{
					sector = sub->render_sector;
					// the planes of this subsector are faked to belong to another sector
					// This means we need the heightsec parts and light info of the render sector, not the actual one.
					fakesector = hw_FakeFlat(&drawctx, sector, in_area, false);
				}

				uint8_t& srf = section_renderflags[Level->sections.SectionIndex(sub->section)];
				if (!(srf & SSRF_PROCESSED))
				{
					srf |= SSRF_PROCESSED;

					SeenSectors.Add(sub->sector->Index());
				}

				// mark subsector as processed - but mark for rendering only if it has an actual area.
				ss_renderflags[sub->Index()] = (sub->numlines > 2) ? SSRF_PROCESSED | SSRF_RENDERALL : SSRF_PROCESSED;
				if (sub->hacked & 1)
					AddHackedSubsector(sub);

				// This is for portal coverage.
				FSectorPortalGroup* portal;

				// AddSubsectorToPortal cannot be called here when using multithreaded processing,
				// because the wall processing code in the worker can also modify the portal state.
				// To avoid costly synchronization for every access to the portal list,
				// the call to AddSubsectorToPortal will be deferred to the worker.
				// (GetPortalGruop only accesses static sector data so this check can be done here, restricting the new job to the minimum possible extent.)
				portal = fakesector->GetPortalGroup(sector_t::ceiling);
				if (portal != nullptr)
				{
					SeenSubsectorPortals.Add(sub->Index());
					// AddSubsectorToPortal(portal, sub);
				}

				portal = fakesector->GetPortalGroup(sector_t::floor);
				if (portal != nullptr)
				{
					SeenSubsectorPortals.Add(sub->Index());
					// AddSubsectorToPortal(portal, sub);
				}
			}
		}
	}
}

void HWVisibleSet::UnclipSubsector(subsector_t* sub)
{
	int count = sub->numlines;
	seg_t* seg = sub->firstline;
	auto& clipper = *mClipper;

	while (count--)
	{
		angle_t startAngle = clipper.GetClipAngle(seg->v2);
		angle_t endAngle = clipper.GetClipAngle(seg->v1);

		// Back side, i.e. backface culling	- read: endAngle >= startAngle!
		if (startAngle - endAngle >= ANGLE_180)
		{
			clipper.SafeRemoveClipRange(startAngle, endAngle);
			clipper.SetBlocked(false);
		}
		seg++;
	}
}

void HWVisibleSet::AddLines(subsector_t* sub, sector_t* sector)
{
	currentsector = sector;
	currentsubsector = sub;

	if (sub->polys != nullptr)
	{
		AddPolyobjs(sub);
	}
	else
	{
		int count = sub->numlines;
		seg_t* seg = sub->firstline;

		while (count--)
		{
			if (seg->linedef == nullptr)
			{
				if (!(sub->flags & SSECMF_DRAWN)) AddLine(seg, mClipPortal != nullptr);
			}
			else if (!(seg->sidedef->Flags & WALLF_POLYOBJ))
			{
				AddLine(seg, mClipPortal != nullptr);
			}
			seg++;
		}
	}
}

void HWVisibleSet::AddLine(seg_t* seg, bool portalclip)
{
#ifdef _DEBUG
	if (seg->linedef && seg->linedef->Index() == 38)
	{
		int a = 0;
	}
#endif

	sector_t* backsector = nullptr;

	if (portalclip)
	{
		int clipres = mClipPortal->ClipSeg(seg, Viewpoint.Pos);
		if (clipres == PClip_InFront) return;
	}

	auto& clipper = *mClipper;
	angle_t startAngle = clipper.GetClipAngle(seg->v2);
	angle_t endAngle = clipper.GetClipAngle(seg->v1);

	// Back side, i.e. backface culling	- read: endAngle >= startAngle!
	if (startAngle - endAngle < ANGLE_180)
	{
		return;
	}

	if (seg->sidedef == nullptr)
	{
		if (!(currentsubsector->flags & SSECMF_DRAWN))
		{
			if (clipper.SafeCheckRange(startAngle, endAngle))
			{
				currentsubsector->flags |= SSECMF_DRAWN;
			}
		}
		return;
	}

	if (!clipper.SafeCheckRange(startAngle, endAngle))
	{
		return;
	}

	uint8_t ispoly = uint8_t(seg->sidedef->Flags & WALLF_POLYOBJ);

	if (!seg->backsector)
	{
		if (!(seg->sidedef->Flags & WALLF_DITHERTRANS_MID)) clipper.SafeAddClipRange(startAngle, endAngle);
	}
	else if (!ispoly)	// Two-sided polyobjects never obstruct the view
	{
		if (currentsector->sectornum == seg->backsector->sectornum)
		{
			if (!seg->linedef->isVisualPortal())
			{
				auto tex = TexMan.GetGameTexture(seg->sidedef->GetTexture(side_t::mid), true);
				if (!tex || !tex->isValid())
				{
					// nothing to do here!
					validcount.line[seg->linedef->Index()] = validcount.current;
					return;
				}
			}
			backsector = currentsector;
		}
		else
		{
			// clipping checks are only needed when the backsector is not the same as the front sector
			if (in_area == area_default) in_area = hw_CheckViewArea(seg->v1, seg->v2, seg->frontsector, seg->backsector);

			backsector = hw_FakeFlat(&drawctx, seg->backsector, in_area, true);

			if (hw_CheckClip(seg->sidedef, currentsector, backsector, &ClipFrustum))
			{
				clipper.SafeAddClipRange(startAngle, endAngle);
			}
		}
	}
	else
	{
		// Backsector for polyobj segs is always the containing sector itself
		backsector = currentsector;
	}

	seg->linedef->flags |= ML_MAPPED;

	if (ispoly || validcount.line[seg->linedef->Index()] != validcount.current)
	{
		if (!ispoly) validcount.line[seg->linedef->Index()] = validcount.current;

		if (gl_render_walls)
		{
			if (seg->sidedef)
			{
				SeenSides.Add(seg->sidedef->Index());
				rendered_lines++;
			}
		}
	}
}

void HWVisibleSet::AddPolyobjs(subsector_t* sub)
{
	// This function isn't thread safe, but polyobjs are very rare. Take the performance hit for now.
	static std::mutex mutex;
	std::unique_lock lock(mutex);

	if (sub->BSP == nullptr || sub->BSP->bDirty)
	{
		sub->BuildPolyBSP();
	}
	if (sub->BSP->Nodes.Size() == 0)
	{
		PolySubsector(&sub->BSP->Subsectors[0]);
	}
	else
	{
		RenderPolyBSPNode(&sub->BSP->Nodes.Last());
	}
}

void HWVisibleSet::RenderPolyBSPNode(void* node)
{
	while (!((size_t)node & 1))  // Keep going until found a subsector
	{
		node_t* bsp = (node_t*)node;

		// Decide which side the view point is on.
		int side = R_PointOnSide(viewx, viewy, bsp);

		// Recursively divide front space (toward the viewer).
		RenderPolyBSPNode(bsp->children[side]);

		// Possibly divide back space (away from the viewer).
		side ^= 1;

		// It is not necessary to use the slower precise version here
		if (!mClipper->CheckBox(bsp->bbox[side]))
		{
			return;
		}

		node = bsp->children[side];
	}
	PolySubsector((subsector_t*)((uint8_t*)node - 1));
}

void HWVisibleSet::PolySubsector(subsector_t* sub)
{
	int count = sub->numlines;
	seg_t* line = sub->firstline;

	while (count--)
	{
		if (line->linedef)
		{
			AddLine(line, mClipPortal != nullptr);
		}
		line++;
	}
}

void HWVisibleSet::AddHackedSubsector(subsector_t* sub)
{
	if (Level->maptype != MAPTYPE_HEXEN)
	{
		SeenHackedSubsectors.Add(sub->Index());
	}
}

static bool PointOnLine(const DVector2& pos, const linebase_t* line)
{
	double v = (pos.Y - line->v1->fY()) * line->Delta().X + (line->v1->fX() - pos.X) * line->Delta().Y;
	return fabs(v) <= EQUAL_EPSILON;
}

// Adds lines that lie directly on the portal boundary.
// Only two-sided lines will be handled here, and no polyobjects
void HWVisibleSet::AddSpecialPortalLines(subsector_t* sub, sector_t* sector, linebase_t* line)
{
	currentsector = sector;
	currentsubsector = sub;

	int count = sub->numlines;
	seg_t* seg = sub->firstline;

	while (count--)
	{
		if (seg->linedef != nullptr && seg->PartnerSeg != nullptr)
		{
			if (PointOnLine(seg->v1->fPos(), line) && PointOnLine(seg->v2->fPos(), line))
				AddLine(seg, false);
		}
		seg++;
	}
}

/////////////////////////////////////////////////////////////////////////////

HWVisibleSetThreads::HWVisibleSetThreads()
{
	SliceCount = std::max((int)(std::thread::hardware_concurrency() * 3 / 4), 1);
	Slices.Resize(SliceCount);
	WorkFlags.resize(SliceCount);

	size_t threadCount = SliceCount - 1;
	while (Threads.size() < threadCount)
	{
		int sliceIndex = Threads.size() + 1;
		Threads.push_back(std::thread([=]() { WorkerMain(sliceIndex); }));
	}
}

HWVisibleSetThreads::~HWVisibleSetThreads()
{
	std::unique_lock lock(Mutex);
	StopFlag = true;
	lock.unlock();
	WorkCondvar.notify_all();
	for (auto& thread : Threads)
		thread.join();
}

CVAR(Int, gl_debug_slice, -1, 0);
extern glcycle_t MTWait, WTTotal;

void HWVisibleSetThreads::FindPVS(HWDrawInfo* di)
{
	WTTotal.Clock();

	// Tell workers there's work to do!
	std::unique_lock lock(Mutex);
	DrawInfo = di;
	for (int i = 0; i < SliceCount; i++)
		WorkFlags[i] = true;
	lock.unlock();
	WorkCondvar.notify_all();

	// Process slice zero ourselves
	Slices[0].FindPVS(DrawInfo, 0, SliceCount);

	// Wait for all workers
	MTWait.Clock();
	lock.lock();
	WorkFlags[0] = false;
	while (true)
	{
		bool allDone = true;
		for (int i = 0; i < SliceCount; i++)
		{
			if (WorkFlags[i])
			{
				allDone = false;
				break;
			}
		}
		if (allDone)
			break;
		DoneCondvar.wait(lock);
	}
	lock.unlock();
	MTWait.Unclock();
	WTTotal.Unclock();

	// Merge results
	for (int i = 0; i < SliceCount; i++)
	{
		if (gl_debug_slice != -1 && gl_debug_slice < SliceCount)
			i = gl_debug_slice;

		for (int sectorIndex : Slices[i].SeenSectors.Get())
			di->SeenSectors.Add(sectorIndex);
		for (int sideIndex : Slices[i].SeenSides.Get())
			di->SeenSides.Add(sideIndex);
		for (int subIndex : Slices[i].SeenSubsectors.Get())
			di->SeenSubsectors.Add(subIndex);
		for (int subIndex : Slices[i].SeenHackedSubsectors.Get())
			di->SeenHackedSubsectors.Add(subIndex);
		for (int subIndex : Slices[i].SeenSubsectorPortals.Get())
			di->SeenSubsectorPortals.Add(subIndex);

		if (gl_debug_slice != -1)
			break;
	}
}

void HWVisibleSetThreads::WorkerMain(int sliceIndex)
{
	std::unique_lock lock(Mutex);
	while (true)
	{
		if (StopFlag)
			break;
		if (WorkFlags[sliceIndex])
		{
			lock.unlock();
			Slices[sliceIndex].FindPVS(DrawInfo, sliceIndex, SliceCount);
			lock.lock();

			if (gl_debug_slice == -1)
			{
				bool isLeft = sliceIndex % 2 == 0;
				int left = isLeft ? sliceIndex : sliceIndex - 1;
				int right = isLeft ? sliceIndex + 1 : sliceIndex;
				if (right < SliceCount && ((isLeft && !WorkFlags[right]) || (!isLeft && !WorkFlags[left])))
				{
					// Other side is also done. Merge results.
					lock.unlock();
					for (int sectorIndex : Slices[right].SeenSectors.Get())
						Slices[left].SeenSectors.Add(sectorIndex);
					for (int sideIndex : Slices[right].SeenSides.Get())
						Slices[left].SeenSides.Add(sideIndex);
					for (int subIndex : Slices[right].SeenSubsectors.Get())
						Slices[left].SeenSubsectors.Add(subIndex);
					for (int subIndex : Slices[right].SeenHackedSubsectors.Get())
						Slices[left].SeenHackedSubsectors.Add(subIndex);
					for (int subIndex : Slices[right].SeenSubsectorPortals.Get())
						Slices[left].SeenSubsectorPortals.Add(subIndex);
					Slices[right].SeenSectors.Clear();
					Slices[right].SeenSides.Clear();
					Slices[right].SeenSubsectors.Clear();
					Slices[right].SeenHackedSubsectors.Clear();
					Slices[right].SeenSubsectorPortals.Clear();
					lock.lock();
				}
			}

			WorkFlags[sliceIndex] = false;

			bool allDone = true;
			for (int i = 0; i < SliceCount; i++)
			{
				if (WorkFlags[i])
				{
					allDone = false;
					break;
				}
			}

			if (allDone)
				DoneCondvar.notify_all();
		}
		WorkCondvar.wait(lock);
	}
}

/////////////////////////////////////////////////////////////////////////////

void CameraFrustum::Set(const VSMatrix& worldToProjection, const DVector3& viewpoint)
{
	Planes[0] = NearFrustum(worldToProjection);
	Planes[1] = LeftFrustum(worldToProjection);
	Planes[2] = TopFrustum(worldToProjection);
	Planes[3] = RightFrustum(worldToProjection);
	Planes[4] = BottomFrustum(worldToProjection);
	Planes[5] = FarFrustum(worldToProjection);

	// Move back near plane to be slightly behind the camera position
	Planes[0].W = -(Planes[0].XYZ() | viewpoint.ToXZY() - 1.0);

	for (int i = 0; i < 6; i++)
	{
		AbsPlaneNormals[i] = Planes[i].XYZ();
		AbsPlaneNormals[i].X = std::abs(AbsPlaneNormals[i].X);
		AbsPlaneNormals[i].Y = std::abs(AbsPlaneNormals[i].Y);
		AbsPlaneNormals[i].Z = std::abs(AbsPlaneNormals[i].Z);
	}
}

DVector4 CameraFrustum::LeftFrustum(const VSMatrix& matrix)
{
	return Normalize(DVector4(
		matrix.mMatrix[3 + 0 * 4] + matrix.mMatrix[0 + 0 * 4],
		matrix.mMatrix[3 + 1 * 4] + matrix.mMatrix[0 + 1 * 4],
		matrix.mMatrix[3 + 2 * 4] + matrix.mMatrix[0 + 2 * 4],
		matrix.mMatrix[3 + 3 * 4] + matrix.mMatrix[0 + 3 * 4]));
}

DVector4 CameraFrustum::RightFrustum(const VSMatrix& matrix)
{
	return Normalize(DVector4(
		matrix.mMatrix[3 + 0 * 4] - matrix.mMatrix[0 + 0 * 4],
		matrix.mMatrix[3 + 1 * 4] - matrix.mMatrix[0 + 1 * 4],
		matrix.mMatrix[3 + 2 * 4] - matrix.mMatrix[0 + 2 * 4],
		matrix.mMatrix[3 + 3 * 4] - matrix.mMatrix[0 + 3 * 4]));
}

DVector4 CameraFrustum::TopFrustum(const VSMatrix& matrix)
{
	return Normalize(DVector4(
		matrix.mMatrix[3 + 0 * 4] - matrix.mMatrix[1 + 0 * 4],
		matrix.mMatrix[3 + 1 * 4] - matrix.mMatrix[1 + 1 * 4],
		matrix.mMatrix[3 + 2 * 4] - matrix.mMatrix[1 + 2 * 4],
		matrix.mMatrix[3 + 3 * 4] - matrix.mMatrix[1 + 3 * 4]));
}

DVector4 CameraFrustum::BottomFrustum(const VSMatrix& matrix)
{
	return Normalize(DVector4(
		matrix.mMatrix[3 + 0 * 4] + matrix.mMatrix[1 + 0 * 4],
		matrix.mMatrix[3 + 1 * 4] + matrix.mMatrix[1 + 1 * 4],
		matrix.mMatrix[3 + 2 * 4] + matrix.mMatrix[1 + 2 * 4],
		matrix.mMatrix[3 + 3 * 4] + matrix.mMatrix[1 + 3 * 4]));
}

DVector4 CameraFrustum::NearFrustum(const VSMatrix& matrix)
{
	return Normalize(DVector4(
		matrix.mMatrix[3 + 0 * 4] + matrix.mMatrix[2 + 0 * 4],
		matrix.mMatrix[3 + 1 * 4] + matrix.mMatrix[2 + 1 * 4],
		matrix.mMatrix[3 + 2 * 4] + matrix.mMatrix[2 + 2 * 4],
		matrix.mMatrix[3 + 3 * 4] + matrix.mMatrix[2 + 3 * 4]));
}

DVector4 CameraFrustum::FarFrustum(const VSMatrix& matrix)
{
	return Normalize(DVector4(
		matrix.mMatrix[3 + 0 * 4] - matrix.mMatrix[2 + 0 * 4],
		matrix.mMatrix[3 + 1 * 4] - matrix.mMatrix[2 + 1 * 4],
		matrix.mMatrix[3 + 2 * 4] - matrix.mMatrix[2 + 2 * 4],
		matrix.mMatrix[3 + 3 * 4] - matrix.mMatrix[2 + 3 * 4]));
}

DVector4 CameraFrustum::Normalize(DVector4 v)
{
	double length = std::sqrt(v.XYZ() | v.XYZ());
	if (length > DBL_EPSILON)
	{
		double rcpLength = 1.0 / length;
		v.X *= rcpLength;
		v.Y *= rcpLength;
		v.Z *= rcpLength;
		v.W *= rcpLength;
	}
	return v;
}
