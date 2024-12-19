/*
**  Level mesh collision detection
**  Copyright (c) 2018 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
*/

#include "hw_collision.h"
#include "hw_levelmesh.h"
#include "v_video.h"
#include "printf.h"
#include <algorithm>
#include <functional>
#include <cfloat>
#ifndef NO_SSE
#include <immintrin.h>
#endif

CPUAccelStruct::CPUAccelStruct(LevelMesh* mesh) : Mesh(mesh)
{
	// Find out how many segments we should split the map into
	DynamicBLAS.resize(32);
	IndexesPerBLAS = ((Mesh->Mesh.Indexes.size() + 2) / 3 / DynamicBLAS.size() + 1) * 3;
	InstanceCount = (Mesh->Mesh.IndexCount + IndexesPerBLAS - 1) / IndexesPerBLAS;

	// Create a BLAS for each segment in use
	for (int instance = 0; instance < InstanceCount; instance++)
	{
		int indexStart = instance * IndexesPerBLAS;
		int indexEnd = std::min(indexStart + IndexesPerBLAS, Mesh->Mesh.IndexCount);
		DynamicBLAS[instance] = CreateBLAS(indexStart, indexEnd - indexStart);
	}

	CreateTLAS();
	Upload();
}

CPUAccelStruct::~CPUAccelStruct()
{
}

TraceHit CPUAccelStruct::FindFirstHit(const FVector3& rayStart, const FVector3& rayEnd)
{
	RayBBox ray(rayStart, rayEnd);
	TraceHit hit;
	FindFirstHit(ray, TLAS.Root, &hit);
	return hit;
}

void CPUAccelStruct::FindFirstHit(const RayBBox& ray, int a, TraceHit* hit)
{
	if (IntersectionTest::ray_aabb(ray, TLAS.Nodes[a].aabb) == IntersectionTest::overlap)
	{
		if (TLAS.Nodes[a].IsLeaf())
		{
			int blasIndex = TLAS.Nodes[a].blas_index;
			TraceHit blasHit = DynamicBLAS[blasIndex]->FindFirstHit(ray.start, ray.end);
			if (blasHit.fraction < hit->fraction)
			{
				hit->fraction = blasHit.fraction;
				hit->triangle = (IndexesPerBLAS * blasIndex) / 3 + blasHit.triangle;
				hit->b = blasHit.b;
				hit->c = blasHit.c;
			}
		}
		else
		{
			FindFirstHit(ray, TLAS.Nodes[a].left, hit);
			FindFirstHit(ray, TLAS.Nodes[a].right, hit);
		}
	}
}

extern cycle_t DynamicBLASTime;

void CPUAccelStruct::Update()
{
	if (Mesh->UploadRanges.Index.GetRanges().Size() == 0)
		return;

	DynamicBLASTime.ResetAndClock();
	InstanceCount = (Mesh->Mesh.IndexCount + IndexesPerBLAS - 1) / IndexesPerBLAS;

	std::vector<bool> needsUpdate(InstanceCount);
	for (const MeshBufferRange& range : Mesh->UploadRanges.Index.GetRanges())
	{
		int start = range.Start / IndexesPerBLAS;
		int end = range.End / IndexesPerBLAS;
		for (int i = start; i < end; i++)
		{
			needsUpdate[i] = true;
		}
	}

	for (int instance = 0; instance < InstanceCount; instance++)
	{
		if (needsUpdate[instance])
		{
			int indexStart = instance * IndexesPerBLAS;
			int indexEnd = std::min(indexStart + IndexesPerBLAS, Mesh->Mesh.IndexCount);
			DynamicBLAS[instance] = CreateBLAS(indexStart, indexEnd - indexStart);
		}
	}
	DynamicBLASTime.Unclock();

	CreateTLAS();
	Upload();
}

std::unique_ptr<CPUBottomLevelAccelStruct> CPUAccelStruct::CreateBLAS(int indexStart, int indexCount)
{
	auto accelstruct = std::make_unique<CPUBottomLevelAccelStruct>(Mesh->Mesh.Vertices.Data(), Mesh->Mesh.Vertices.Size(), &Mesh->Mesh.Indexes[indexStart], indexCount, Scratch);
	if (accelstruct->GetRoot() == -1)
		return {};
	return accelstruct;
}

static FVector3 SwapYZ(const FVector3& v)
{
	return FVector3(v.X, v.Z, v.Y);
}

void CPUAccelStruct::Upload()
{
	if (screen->IsRayQueryEnabled())
		return;

	unsigned int count = (unsigned int)TLAS.Nodes.size();
	for (auto& blas : DynamicBLAS)
	{
		if (blas)
		{
			count += (unsigned int)blas->GetNodes().size();
		}
	}

	if (Mesh->Mesh.Nodes.Size() < count)
	{
		Mesh->Mesh.Nodes.Resize(std::max(count * 2, (unsigned int)10000));
	}

	Mesh->Mesh.RootNode = TLAS.Root;

	auto& destnodes = Mesh->Mesh.Nodes;

	// Copy the BLAS nodes to the mesh node list and remember their locations
	int offset = TLAS.Nodes.size();
	int instance = 0;
	std::vector<int> blasOffsets(DynamicBLAS.size());
	for (auto& blas : DynamicBLAS)
	{
		if (blas)
		{
			int blasStart = offset;
			int indexStart = instance * IndexesPerBLAS;
			blasOffsets[instance] = blasStart;

			for (const auto& node : blas->GetNodes())
			{
				CollisionNode& info = destnodes[offset];
				info.center = SwapYZ(node.aabb.Center);
				info.extents = SwapYZ(node.aabb.Extents);
				info.left = node.left != -1 ? blasStart + node.left : -1;
				info.right = node.right != -1 ? blasStart + node.right : -1;
				info.element_index = node.element_index != -1 ? indexStart + node.element_index : -1;
				offset++;
			}
			instance++;
		}
	}

	// Copy the TLAS nodes and redirect the leafs to the BLAS roots
	offset = 0;
	for (const auto& node : TLAS.Nodes)
	{
		CollisionNode& info = destnodes[offset];
		info.center = SwapYZ(node.aabb.Center);
		info.extents = SwapYZ(node.aabb.Extents);

		if (node.left != -1 && TLAS.Nodes[node.left].blas_index != -1)
		{
			int blas_index = TLAS.Nodes[node.left].blas_index;
			info.left = blasOffsets[blas_index] + DynamicBLAS[blas_index]->GetRoot();
		}
		else
		{
			info.left = node.left;
		}

		if (node.right != -1 && TLAS.Nodes[node.right].blas_index != -1)
		{
			int blas_index = TLAS.Nodes[node.right].blas_index;
			info.right = blasOffsets[blas_index] + DynamicBLAS[blas_index]->GetRoot();
		}
		else
		{
			info.right = node.right;
		}

		info.element_index = -1;
		offset++;
	}

	Mesh->UploadRanges.Node.Clear();
	Mesh->UploadRanges.Node.Add(0, (int)count);
}

void CPUAccelStruct::CreateTLAS()
{
	Scratch.leafs.clear();
	Scratch.leafs.reserve(InstanceCount);
	Scratch.centroids.clear();
	Scratch.centroids.reserve(DynamicBLAS.size());
	for (int i = 0; i < InstanceCount; i++)
	{
		if (DynamicBLAS[i])
		{
			Scratch.leafs.push_back(i);
			Scratch.centroids.push_back(FVector4(DynamicBLAS[i]->GetBBox().Center, 1.0f));
		}
		else
		{
			Scratch.leafs.push_back(0);
			Scratch.centroids.push_back(FVector4(-1000000.0f, -1000000.0f, -1000000.0f, 1.0f));
		}
	}

	size_t neededbuffersize = InstanceCount * 2;
	if (Scratch.workbuffer.size() < neededbuffersize)
		Scratch.workbuffer.resize(neededbuffersize);

	TLAS.Nodes.clear();
	TLAS.Root = Subdivide(Scratch.leafs.data(), (int)Scratch.leafs.size(), Scratch.centroids.data(), Scratch.workbuffer.data());
}

int CPUAccelStruct::Subdivide(int* instances, int numInstances, const FVector4* centroids, int* workBuffer)
{
	if (numInstances == 0)
		return -1;

	// Find bounding box and median of the instance centroids
	FVector3 median(0.0f, 0.0f, 0.0f);
	FVector3 min = DynamicBLAS[instances[0]]->GetBBox().min;
	FVector3 max = DynamicBLAS[instances[0]]->GetBBox().max;
	for (int i = 0; i < numInstances; i++)
	{
		const CollisionBBox& bbox = DynamicBLAS[instances[i]]->GetBBox();

		min.X = std::min(min.X, bbox.min.X);
		min.Y = std::min(min.Y, bbox.min.Y);
		min.Z = std::min(min.Z, bbox.min.Z);

		max.X = std::max(max.X, bbox.max.X);
		max.Y = std::max(max.Y, bbox.max.Y);
		max.Z = std::max(max.Z, bbox.max.Z);

		median += centroids[instances[i]].XYZ();
	}
	median /= (float)numInstances;

	// For numerical stability
	min.X -= 0.1f;
	min.Y -= 0.1f;
	min.Z -= 0.1f;
	max.X += 0.1f;
	max.Y += 0.1f;
	max.Z += 0.1f;

	if (numInstances == 1) // Leaf node
	{
		TLAS.Nodes.push_back(Node(min, max, instances[0]));
		return (int)TLAS.Nodes.size() - 1;
	}

	// Find the longest axis
	float axis_lengths[3] =
	{
		max.X - min.X,
		max.Y - min.Y,
		max.Z - min.Z
	};

	int axis_order[3] = { 0, 1, 2 };
	std::sort(axis_order, axis_order + 3, [&](int a, int b) { return axis_lengths[a] > axis_lengths[b]; });

	// Try split at longest axis, then if that fails the next longest, and then the remaining one
	int left_count, right_count;
	FVector3 axis;
	for (int attempt = 0; attempt < 3; attempt++)
	{
		// Find the split plane for axis
		switch (axis_order[attempt])
		{
		default:
		case 0: axis = FVector3(1.0f, 0.0f, 0.0f); break;
		case 1: axis = FVector3(0.0f, 1.0f, 0.0f); break;
		case 2: axis = FVector3(0.0f, 0.0f, 1.0f); break;
		}
		FVector4 plane(axis, -(median | axis)); // plane(axis, -dot(median, axis));

		// Split instances into two
		left_count = 0;
		right_count = 0;
		for (int i = 0; i < numInstances; i++)
		{
			int instance = instances[i];

			float side = centroids[instance] | plane;
			if (side >= 0.0f)
			{
				workBuffer[left_count] = instance;
				left_count++;
			}
			else
			{
				workBuffer[numInstances + right_count] = instance;
				right_count++;
			}
		}

		if (left_count != 0 && right_count != 0)
			break;
	}

	// Check if something went wrong when splitting and do a random split instead
	if (left_count == 0 || right_count == 0)
	{
		left_count = numInstances / 2;
		right_count = numInstances - left_count;
	}
	else
	{
		// Move result back into instances list:
		for (int i = 0; i < left_count; i++)
			instances[i] = workBuffer[i];
		for (int i = 0; i < right_count; i++)
			instances[i + left_count] = workBuffer[numInstances + i];
	}

	// Create child nodes:
	int left_index = -1;
	int right_index = -1;
	if (left_count > 0)
		left_index = Subdivide(instances, left_count, centroids, workBuffer);
	if (right_count > 0)
		right_index = Subdivide(instances + left_count, right_count, centroids, workBuffer);

	TLAS.Nodes.push_back(Node(min, max, left_index, right_index));
	return (int)TLAS.Nodes.size() - 1;
}

void CPUAccelStruct::PrintStats()
{
	for (size_t i = 0; i < DynamicBLAS.size(); i++)
	{
		if (DynamicBLAS[i])
		{
			Printf("#%d avg=%2.3f balanced=%2.3f nodes=%d buildtime=%2.3f ms\n", (int)i, (double)DynamicBLAS[i]->GetAverageDepth(), (double)DynamicBLAS[i]->GetBalancedDepth(), (int)DynamicBLAS[i]->GetNodes().size(), DynamicBLAS[i]->GetBuildTimeMS());
		}
		else
		{
			Printf("#%d unused\n", (int)i);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////

CPUBottomLevelAccelStruct::CPUBottomLevelAccelStruct(const FFlatVertex *vertices, int num_vertices, const unsigned int *elements, int num_elements, AccelStructScratchBuffer& scratch)
	: vertices(vertices), num_vertices(num_vertices), elements(elements), num_elements(num_elements)
{
	int num_triangles = num_elements / 3;
	if (num_triangles <= 0)
		return;

	cycle_t timer;
	timer.ResetAndClock();

	scratch.leafs.clear();
	scratch.leafs.reserve(num_triangles);
	scratch.centroids.clear();
	scratch.centroids.reserve(num_triangles);
	int active_triangles = 0;
	for (int i = 0; i < num_triangles; i++)
	{
		int element_index = i * 3;
		int a = elements[element_index + 0];
		int b = elements[element_index + 1];
		int c = elements[element_index + 2];
		if (a == b)
			continue;

		FVector3 centroid = (vertices[a].fPos() + vertices[b].fPos() + vertices[c].fPos()) * (1.0f / 3.0f);
		scratch.leafs.push_back(i);
		scratch.centroids.push_back(FVector4(centroid, 1.0f));
		active_triangles++;
	}

	size_t neededbuffersize = active_triangles * 2;
	if (scratch.workbuffer.size() < neededbuffersize)
		scratch.workbuffer.resize(neededbuffersize);

	root = Subdivide(scratch.leafs.data(), (int)scratch.leafs.size(), scratch.centroids.data(), scratch.workbuffer.data());

	timer.Unclock();
	buildtime = timer.TimeMS();
}

TraceHit CPUBottomLevelAccelStruct::FindFirstHit(const FVector3 &ray_start, const FVector3 &ray_end)
{
	TraceHit hit;

	if (root == -1)
		return hit;

	// Perform segmented tracing to keep the ray AABB box smaller

	FVector3 ray_dir = ray_end - ray_start;
	float tracedist = (float)ray_dir.Length();
	float segmentlen = std::max(100.0f, tracedist / 20.0f);
	for (float t = 0.0f; t < tracedist; t += segmentlen)
	{
		float segstart = t / tracedist;
		float segend = std::min(t + segmentlen, tracedist) / tracedist;

		FindFirstHit(RayBBox(ray_start + ray_dir * segstart, ray_start + ray_dir * segend), root, &hit);
		if (hit.fraction < 1.0f)
		{
			hit.fraction = segstart * (1.0f - hit.fraction) + segend * hit.fraction;
			break;
		}
	}

	return hit;
}

void CPUBottomLevelAccelStruct::FindFirstHit(const RayBBox &ray, int a, TraceHit *hit)
{
	if (IntersectionTest::ray_aabb(ray, nodes[a].aabb) == IntersectionTest::overlap)
	{
		if (nodes[a].IsLeaf())
		{
			float baryB, baryC;
			float t = IntersectTriangleRay(ray, a, baryB, baryC);
			if (t < hit->fraction)
			{
				hit->fraction = t;
				hit->triangle = nodes[a].element_index / 3;
				hit->b = baryB;
				hit->c = baryC;
			}
		}
		else
		{
			FindFirstHit(ray, nodes[a].left, hit);
			FindFirstHit(ray, nodes[a].right, hit);
		}
	}
}

float CPUBottomLevelAccelStruct::IntersectTriangleRay(const RayBBox &ray, int a, float &barycentricB, float &barycentricC)
{
	const int start_element = nodes[a].element_index;

	FVector3 p[3] =
	{
		vertices[elements[start_element]].fPos(),
		vertices[elements[start_element + 1]].fPos(),
		vertices[elements[start_element + 2]].fPos()
	};

	// Moeller-Trumbore ray-triangle intersection algorithm:

	FVector3 D = ray.end - ray.start;

	// Find vectors for two edges sharing p[0]
	FVector3 e1 = p[1] - p[0];
	FVector3 e2 = p[2] - p[0];

	// Begin calculating determinant - also used to calculate u parameter
	FVector3 P = D ^ e2; // cross(D, e2);
	float det = e1 | P; // dot(e1, P);

	// Backface check
	//if (det < 0.0f)
	//	return 1.0f;

	// If determinant is near zero, ray lies in plane of triangle
	if (det > -FLT_EPSILON && det < FLT_EPSILON)
		return 1.0f;

	float inv_det = 1.0f / det;

	// Calculate distance from p[0] to ray origin
	FVector3 T = ray.start - p[0];

	// Calculate u parameter and test bound
	float u = (T | P) * inv_det; // dot(T, P) * inv_det;

	// Check if the intersection lies outside of the triangle
	if (u < 0.f || u > 1.f)
		return 1.0f;

	// Prepare to test v parameter
	FVector3 Q = T ^ e1; // cross(T, e1);

	// Calculate V parameter and test bound
	float v = (D | Q) * inv_det; // dot(D, Q) * inv_det;

	// The intersection lies outside of the triangle
	if (v < 0.f || u + v  > 1.f)
		return 1.0f;

	float t = (e2 | Q) * inv_det; //dot(e2, Q) * inv_det;
	if (t <= FLT_EPSILON)
		return 1.0f;

	// Return hit location on triangle in barycentric coordinates
	barycentricB = u;
	barycentricC = v;
	
	return t;
}

int CPUBottomLevelAccelStruct::GetMinDepth() const
{
	std::function<int(int, int)> visit;
	visit = [&](int level, int node_index) -> int {
		const Node &node = nodes[node_index];
		if (node.element_index == -1)
			return std::min(visit(level + 1, node.left), visit(level + 1, node.right));
		else
			return level;
	};
	return visit(1, root);
}

int CPUBottomLevelAccelStruct::GetMaxDepth() const
{
	std::function<int(int, int)> visit;
	visit = [&](int level, int node_index) -> int {
		const Node &node = nodes[node_index];
		if (node.element_index == -1)
			return std::max(visit(level + 1, node.left), visit(level + 1, node.right));
		else
			return level;
	};
	return visit(1, root);
}

float CPUBottomLevelAccelStruct::GetAverageDepth() const
{
	std::function<float(int, int)> visit;
	visit = [&](int level, int node_index) -> float {
		const Node &node = nodes[node_index];
		if (node.element_index == -1)
			return visit(level + 1, node.left) + visit(level + 1, node.right);
		else
			return (float)level;
	};
	float depth_sum = visit(1, root);
	int leaf_count = (num_elements / 3);
	return depth_sum / leaf_count;
}

float CPUBottomLevelAccelStruct::GetBalancedDepth() const
{
	return std::log2((float)(num_elements / 3));
}

int CPUBottomLevelAccelStruct::SubdivideLeaf(int* triangles, int num_triangles)
{
	if (num_triangles == 0)
		return -1;

	int element_index = triangles[0] * 3;

	FVector3 min = vertices[elements[element_index]].fPos();
	FVector3 max = min;

	for (int j = 1; j < 3; j++)
	{
		const FVector3& vertex = vertices[elements[element_index + j]].fPos();

		min.X = std::min(min.X, vertex.X);
		min.Y = std::min(min.Y, vertex.Y);
		min.Z = std::min(min.Z, vertex.Z);

		max.X = std::max(max.X, vertex.X);
		max.Y = std::max(max.Y, vertex.Y);
		max.Z = std::max(max.Z, vertex.Z);
	}

	FVector3 margin(0.1f, 0.1f, 0.1f);
	nodes.push_back(Node(min - margin, max + margin, element_index));
	return (int)nodes.size() - 1;
}

// Sadly, this seems to be slower than what the compiler generated :(
// Use it in debug mode anyway as its time critical and faster there
#if !defined(NO_SSE) && defined(_DEBUG)

static const FVector3 axes[3] = { FVector3(-1.0f, 0.0f, 0.0f), FVector3(0.0f, -1.0f, 0.0f), FVector3(0.0f, 0.0f, -1.0f) };

int CPUBottomLevelAccelStruct::Subdivide(int* triangles, int num_triangles, const FVector4* centroids, int* work_buffer)
{
	if (num_triangles <= 1)
		return SubdivideLeaf(triangles, num_triangles);

	// Let the compiler optimize these into registers
	const FFlatVertex* vertices = this->vertices;
	const unsigned int* elements = this->elements;

	// Find bounding box and median of the triangle centroids
	__m128 mmedian = _mm_setzero_ps();
	__m128 mmin = _mm_loadu_ps(reinterpret_cast<const float*>(&vertices[elements[triangles[0] * 3]]));
	__m128 mmax = mmin;
	for (int i = 0; i < num_triangles; i++)
	{
		int v = triangles[i];
		int element_index = v + v + v; // triangles[i] * 3
		for (int j = 0; j < 3; j++)
		{
			__m128 vertex = _mm_loadu_ps(reinterpret_cast<const float*>(&vertices[elements[element_index + j]]));
			mmin = _mm_min_ps(mmin, vertex);
			mmax = _mm_max_ps(mmax, vertex);
		}

		mmedian = _mm_add_ps(mmedian, _mm_loadu_ps(reinterpret_cast<const float*>(&centroids[triangles[i]])));
	}
	mmedian = _mm_div_ps(mmedian, _mm_set1_ps((float)num_triangles));

	// For numerical stability
	mmin = _mm_sub_ps(mmin, _mm_set1_ps(0.1f));
	mmax = _mm_add_ps(mmax, _mm_set1_ps(0.1f));

	// FFlatVertex got Y and Z swapped
	mmin = _mm_shuffle_ps(mmin, mmin, _MM_SHUFFLE(3, 1, 2, 0));
	mmax = _mm_shuffle_ps(mmax, mmax, _MM_SHUFFLE(3, 1, 2, 0));

	float min[4], max[4], median[4], axis_lengths[4];
	_mm_store_ps(min, mmin);
	_mm_store_ps(max, mmax);
	_mm_store_ps(median, mmedian);
	_mm_store_ps(axis_lengths, _mm_sub_ps(mmax, mmin));

	// Find the longest axis
#if 0
	int axis_order[3] = { 0, 1, 2 };
	std::sort(axis_order, axis_order + 3, [&](int a, int b) { return axis_lengths[a] > axis_lengths[b]; });
#else
	int axis_order[3];
	if (axis_lengths[0] >= axis_lengths[1] && axis_lengths[0] >= axis_lengths[2])
	{
		axis_order[0] = 0;
		if (axis_lengths[1] >= axis_lengths[2])
		{
			axis_order[1] = 1;
			axis_order[2] = 2;
		}
		else
		{
			axis_order[1] = 2;
			axis_order[2] = 1;
		}
	}
	else if (axis_lengths[1] >= axis_lengths[0] && axis_lengths[1] >= axis_lengths[2])
	{
		axis_order[0] = 1;
		if (axis_lengths[0] >= axis_lengths[2])
		{
			axis_order[1] = 0;
			axis_order[2] = 2;
		}
		else
		{
			axis_order[1] = 2;
			axis_order[2] = 0;
		}
	}
	else
	{
		axis_order[0] = 2;
		if (axis_lengths[0] >= axis_lengths[1])
		{
			axis_order[1] = 0;
			axis_order[2] = 1;
		}
		else
		{
			axis_order[1] = 1;
			axis_order[2] = 0;
		}
	}
#endif

	// Try split at longest axis, then if that fails the next longest, and then the remaining one
	int left_count, right_count;
	for (int attempt = 0; attempt < 3; attempt++)
	{
		// Find the split plane for axis
		const FVector3& axis = axes[axis_order[attempt]];
		FVector4 plane(axis, median[0] * axis.X + median[1] * axis.Y + median[2] * axis.Z); // plane(axis, -dot(median, axis));

		// Split triangles into two
		left_count = 0;
		right_count = 0;
		for (int i = 0; i < num_triangles; i++)
		{
			int triangle = triangles[i];
			int element_index = triangle * 3;

			float side = centroids[triangles[i]] | plane; // dot(FVector4(centroids[triangles[i]], 1.0f), plane);
			if (side >= 0.0f)
			{
				work_buffer[left_count] = triangle;
				left_count++;
			}
			else
			{
				work_buffer[num_triangles + right_count] = triangle;
				right_count++;
			}
		}

		if (left_count != 0 && right_count != 0)
			break;
	}

	// Check if something went wrong when splitting and do a random split instead
	if (left_count == 0 || right_count == 0)
	{
		left_count = num_triangles / 2;
		right_count = num_triangles - left_count;
	}
	else
	{
		// Move result back into triangles list:
		for (int i = 0; i < left_count; i++)
			triangles[i] = work_buffer[i];
		for (int i = 0; i < right_count; i++)
			triangles[i + left_count] = work_buffer[num_triangles + i];
	}

	// Create child nodes:
	int left_index = -1;
	int right_index = -1;
	if (left_count > 0)
		left_index = Subdivide(triangles, left_count, centroids, work_buffer);
	if (right_count > 0)
		right_index = Subdivide(triangles + left_count, right_count, centroids, work_buffer);

	nodes.push_back(Node(FVector3(min[0], min[1], min[2]), FVector3(max[0], max[1], max[2]), left_index, right_index));
	return (int)nodes.size() - 1;
}

#else

int CPUBottomLevelAccelStruct::Subdivide(int *triangles, int num_triangles, const FVector4 *centroids, int *work_buffer)
{
	if (num_triangles <= 1)
		return SubdivideLeaf(triangles, num_triangles);

	// Let the compiler optimize these into registers
	const FFlatVertex* vertices = this->vertices;
	const unsigned int* elements = this->elements;

	// Find bounding box and median of the triangle centroids
	FVector3 median(0.0f, 0.0f, 0.0f);
	FVector3 min, max;
	min = vertices[elements[triangles[0] * 3]].fPos();
	max = min;
	for (int i = 0; i < num_triangles; i++)
	{
		int element_index = triangles[i] * 3;
		for (int j = 0; j < 3; j++)
		{
			const FVector3 &vertex = vertices[elements[element_index + j]].fPos();

			min.X = std::min(min.X, vertex.X);
			min.Y = std::min(min.Y, vertex.Y);
			min.Z = std::min(min.Z, vertex.Z);

			max.X = std::max(max.X, vertex.X);
			max.Y = std::max(max.Y, vertex.Y);
			max.Z = std::max(max.Z, vertex.Z);
		}

		median += centroids[triangles[i]].XYZ();
	}
	median /= (float)num_triangles;

	// For numerical stability
	min.X -= 0.1f;
	min.Y -= 0.1f;
	min.Z -= 0.1f;
	max.X += 0.1f;
	max.Y += 0.1f;
	max.Z += 0.1f;

	// Find the longest axis
	float axis_lengths[3] =
	{
		max.X - min.X,
		max.Y - min.Y,
		max.Z - min.Z
	};

#if 0
	int axis_order[3] = { 0, 1, 2 };
	std::sort(axis_order, axis_order + 3, [&](int a, int b) { return axis_lengths[a] > axis_lengths[b]; });
#else
	int axis_order[3];
	if (axis_lengths[0] >= axis_lengths[1] && axis_lengths[0] >= axis_lengths[2])
	{
		axis_order[0] = 0;
		if (axis_lengths[1] >= axis_lengths[2])
		{
			axis_order[1] = 1;
			axis_order[2] = 2;
		}
		else
		{
			axis_order[1] = 2;
			axis_order[2] = 1;
		}
	}
	else if (axis_lengths[1] >= axis_lengths[0] && axis_lengths[1] >= axis_lengths[2])
	{
		axis_order[0] = 1;
		if (axis_lengths[0] >= axis_lengths[2])
		{
			axis_order[1] = 0;
			axis_order[2] = 2;
		}
		else
		{
			axis_order[1] = 2;
			axis_order[2] = 0;
		}
	}
	else
	{
		axis_order[0] = 2;
		if (axis_lengths[0] >= axis_lengths[1])
		{
			axis_order[1] = 0;
			axis_order[2] = 1;
		}
		else
		{
			axis_order[1] = 1;
			axis_order[2] = 0;
		}
	}
#endif

	// Try split at longest axis, then if that fails the next longest, and then the remaining one
	int left_count, right_count;
	FVector3 axis;
	for (int attempt = 0; attempt < 3; attempt++)
	{
		// Find the split plane for axis
		switch (axis_order[attempt])
		{
		default:
		case 0: axis = FVector3(1.0f, 0.0f, 0.0f); break;
		case 1: axis = FVector3(0.0f, 1.0f, 0.0f); break;
		case 2: axis = FVector3(0.0f, 0.0f, 1.0f); break;
		}
		FVector4 plane(axis, -(median | axis)); // plane(axis, -dot(median, axis));

		// Split triangles into two
		left_count = 0;
		right_count = 0;
		for (int i = 0; i < num_triangles; i++)
		{
			int triangle = triangles[i];
			int element_index = triangle * 3;

			float side = centroids[triangles[i]] | plane; // dot(FVector4(centroids[triangles[i]], 1.0f), plane);
			if (side >= 0.0f)
			{
				work_buffer[left_count] = triangle;
				left_count++;
			}
			else
			{
				work_buffer[num_triangles + right_count] = triangle;
				right_count++;
			}
		}

		if (left_count != 0 && right_count != 0)
			break;
	}

	// Check if something went wrong when splitting and do a random split instead
	if (left_count == 0 || right_count == 0)
	{
		left_count = num_triangles / 2;
		right_count = num_triangles - left_count;
	}
	else
	{
		// Move result back into triangles list:
		for (int i = 0; i < left_count; i++)
			triangles[i] = work_buffer[i];
		for (int i = 0; i < right_count; i++)
			triangles[i + left_count] = work_buffer[num_triangles + i];
	}

	// Create child nodes:
	int left_index = -1;
	int right_index = -1;
	if (left_count > 0)
		left_index = Subdivide(triangles, left_count, centroids, work_buffer);
	if (right_count > 0)
		right_index = Subdivide(triangles + left_count, right_count, centroids, work_buffer);

	nodes.push_back(Node(min, max, left_index, right_index));
	return (int)nodes.size() - 1;
}

#endif

/////////////////////////////////////////////////////////////////////////////

static const uint32_t clearsignbitmask[] = { 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff };

IntersectionTest::OverlapResult IntersectionTest::ray_aabb(const RayBBox &ray, const CollisionBBox &aabb)
{
#ifndef NO_SSE

	__m128 v = _mm_loadu_ps(&ray.v.X);
	__m128 w = _mm_loadu_ps(&ray.w.X);
	__m128 h = _mm_loadu_ps(&aabb.Extents.X);
	__m128 c = _mm_sub_ps(_mm_loadu_ps(&ray.c.X), _mm_loadu_ps(&aabb.Center.X));

	__m128 clearsignbit = _mm_loadu_ps(reinterpret_cast<const float*>(clearsignbitmask));

	__m128 abs_c = _mm_and_ps(c, clearsignbit);
	int mask = _mm_movemask_ps(_mm_cmpgt_ps(abs_c, _mm_add_ps(v, h)));
	if (mask & 7)
		return disjoint;

	__m128 c1 = _mm_shuffle_ps(c, c, _MM_SHUFFLE(3, 0, 0, 1)); // c.Y, c.X, c.X
	__m128 c2 = _mm_shuffle_ps(c, c, _MM_SHUFFLE(3, 1, 2, 2)); // c.Z, c.Z, c.Y
	__m128 w1 = _mm_shuffle_ps(w, w, _MM_SHUFFLE(3, 1, 2, 2)); // w.Z, w.Z, w.Y
	__m128 w2 = _mm_shuffle_ps(w, w, _MM_SHUFFLE(3, 0, 0, 1)); // w.Y, w.X, w.X
	__m128 lhs = _mm_and_ps(_mm_sub_ps(_mm_mul_ps(c1, w1), _mm_mul_ps(c2, w2)), clearsignbit);

	__m128 h1 = _mm_shuffle_ps(h, h, _MM_SHUFFLE(3, 0, 0, 1)); // h.Y, h.X, h.X
	__m128 h2 = _mm_shuffle_ps(h, h, _MM_SHUFFLE(3, 1, 2, 2)); // h.Z, h.Z, h.Y
	__m128 v1 = _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 1, 2, 2)); // v.Z, v.Z, v.Y
	__m128 v2 = _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 0, 0, 1)); // v.Y, v.X, v.X
	__m128 rhs = _mm_add_ps(_mm_mul_ps(h1, v1), _mm_mul_ps(h2, v2));

	mask = _mm_movemask_ps(_mm_cmpgt_ps(lhs, rhs));
	return (mask & 7) ? disjoint : overlap;

#else
	const FVector3 &v = ray.v;
	const FVector3 &w = ray.w;
	const FVector3 &h = aabb.Extents;
	auto c = ray.c - aabb.Center;

	if (std::abs(c.X) > v.X + h.X || std::abs(c.Y) > v.Y + h.Y || std::abs(c.Z) > v.Z + h.Z)
		return disjoint;

	if (std::abs(c.Y * w.Z - c.Z * w.Y) > h.Y * v.Z + h.Z * v.Y ||
		std::abs(c.X * w.Z - c.Z * w.X) > h.X * v.Z + h.Z * v.X ||
		std::abs(c.X * w.Y - c.Y * w.X) > h.X * v.Y + h.Y * v.X)
		return disjoint;

	return overlap;
#endif
}
