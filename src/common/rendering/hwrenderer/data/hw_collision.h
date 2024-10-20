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

#pragma once

#include "common/utility/vectors.h"
#include "flatvertices.h"
#include <vector>
#include <cmath>
#include <memory>

class LevelMesh;
class CPUBottomLevelAccelStruct;

struct TraceHit
{
	float fraction = 1.0f;
	int triangle = -1;
	float b = 0.0f;
	float c = 0.0f;
};

struct CollisionNode
{
	FVector3 center;
	float padding1;
	FVector3 extents;
	float padding2;
	int left;
	int right;
	int element_index;
	int padding3;
};

class CollisionBBox
{
public:
	CollisionBBox() = default;

	CollisionBBox(const FVector3& aabb_min, const FVector3& aabb_max)
	{
		min = aabb_min;
		max = aabb_max;
		auto halfmin = aabb_min * 0.5f;
		auto halfmax = aabb_max * 0.5f;
		Center = halfmax + halfmin;
		Extents = halfmax - halfmin;
	}

	FVector3 min;
	FVector3 max;
	FVector3 Center;
	FVector3 Extents;
	float ssePadding = 0.0f; // Needed to safely load Extents directly into a sse register
};

class RayBBox
{
public:
	RayBBox(const FVector3& ray_start, const FVector3& ray_end) : start(ray_start), end(ray_end)
	{
		c = (ray_start + ray_end) * 0.5f;
		w = ray_end - c;
		v.X = std::abs(w.X);
		v.Y = std::abs(w.Y);
		v.Z = std::abs(w.Z);
	}

	FVector3 start, end;
	FVector3 c, w, v;
	float ssePadding = 0.0f; // Needed to safely load v directly into a sse register
};

class AccelStructScratchBuffer
{
public:
	std::vector<int> leafs;
	std::vector<FVector3> centroids;
	std::vector<int> workbuffer;
};

class CPUAccelStruct
{
public:
	CPUAccelStruct(LevelMesh* mesh);
	~CPUAccelStruct();

	void Update();
	TraceHit FindFirstHit(const FVector3& rayStart, const FVector3& rayEnd);

private:
	void FindFirstHit(const RayBBox& ray, int a, TraceHit* hit);
	void CreateTLAS();
	int Subdivide(int* instances, int numInstances, const FVector3* centroids, int* workBuffer);
	std::unique_ptr<CPUBottomLevelAccelStruct> CreateBLAS(int indexStart, int indexCount);
	void Upload();

	LevelMesh* Mesh = nullptr;

	struct Node
	{
		Node() = default;
		Node(const FVector3& aabb_min, const FVector3& aabb_max, int blas_index) : aabb(aabb_min, aabb_max), blas_index(blas_index) { }
		Node(const FVector3& aabb_min, const FVector3& aabb_max, int left, int right) : aabb(aabb_min, aabb_max), left(left), right(right) { }

		bool IsLeaf() const { return blas_index != -1; }

		CollisionBBox aabb;
		int left = -1;
		int right = -1;
		int blas_index = -1;
	};

	struct
	{
		std::vector<Node> Nodes;
		int Root = 0;
	} TLAS;

	std::vector<std::unique_ptr<CPUBottomLevelAccelStruct>> DynamicBLAS;
	int IndexesPerBLAS = 0;
	int InstanceCount = 0;

	AccelStructScratchBuffer Scratch;
};

class CPUBottomLevelAccelStruct
{
public:
	CPUBottomLevelAccelStruct(const FFlatVertex *vertices, int num_vertices, const unsigned int *elements, int num_elements, AccelStructScratchBuffer& scratch);

	int GetMinDepth() const;
	int GetMaxDepth() const;
	float GetAverageDepth() const;
	float GetBalancedDepth() const;

	const CollisionBBox &GetBBox() const { return nodes[root].aabb; }

	TraceHit FindFirstHit(const FVector3 &ray_start, const FVector3 &ray_end);

	struct Node
	{
		Node() = default;
		Node(const FVector3 &aabb_min, const FVector3 &aabb_max, int element_index) : aabb(aabb_min, aabb_max), element_index(element_index) { }
		Node(const FVector3 &aabb_min, const FVector3 &aabb_max, int left, int right) : aabb(aabb_min, aabb_max), left(left), right(right) { }

		bool IsLeaf() const { return element_index != -1; }

		CollisionBBox aabb;
		int left = -1;
		int right = -1;
		int element_index = -1;
	};

	const std::vector<Node>& GetNodes() const { return nodes; }
	int GetRoot() const { return root; }

private:
	const FFlatVertex* vertices = nullptr;
	const int num_vertices = 0;
	const unsigned int *elements = nullptr;
	int num_elements = 0;

	std::vector<Node> nodes;
	int root = -1;

	void FindFirstHit(const RayBBox& ray, int a, TraceHit* hit);
	float IntersectTriangleRay(const RayBBox &ray, int a, float &barycentricB, float &barycentricC);
	int Subdivide(int *triangles, int num_triangles, const FVector3 *centroids, int *work_buffer);
};

class IntersectionTest
{
public:
	enum OverlapResult
	{
		disjoint,
		overlap
	};

	static OverlapResult ray_aabb(const RayBBox &ray, const CollisionBBox &box);
};
