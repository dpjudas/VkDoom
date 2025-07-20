#pragma once

#include "vectors.h"

class LevelMesh;

struct LightProbe
{
	FVector3 position;
	int index = 0;
};

struct LightProbeTarget
{
	int index = 0; // parameter for renderstate.SetLightProbe
};

class LightProbeIncrementalBuilder
{
public:
	void Step(const TArray<LightProbe>& probes, std::function<void(int probeIndex, const LightProbe& probe)> renderScene);
	void Full(const TArray<LightProbe>& probes, std::function<void(int probeIndex, const LightProbe& probe)> renderScene);

	auto GetStep() const { return lastIndex; }

private:
	int lastIndex = 0;
	int collected = 0;
	int cubemapsAllocated = 0;
	int iterations = 0;
};

struct ProbeNode
{
	FVector2 center;
	FVector2 extents;
	int left;
	int right;
	int probeIndex;
	int padding0;
	FVector3 probePos;
	float padding1;
};

class LightProbeAABBTree
{
public:
	LightProbeAABBTree(LevelMesh* mesh);
	~LightProbeAABBTree();

	void Update();

	int FindClosestProbe(FVector3 pos, float extent);

private:
	void Create(const TArray<LightProbe>& probes);
	int Subdivide(int* instances, int numInstances, const FVector3* centroids, int* workBuffer, const TArray<LightProbe>& probes);
	void Upload();

	LevelMesh* Mesh = nullptr;

	struct BBox
	{
		BBox() = default;

		BBox(const FVector2& aabb_min, const FVector2& aabb_max)
		{
			min = aabb_min;
			max = aabb_max;
			auto halfmin = aabb_min * 0.5f;
			auto halfmax = aabb_max * 0.5f;
			Center = halfmax + halfmin;
			Extents = halfmax - halfmin;
		}

		FVector2 min;
		FVector2 max;
		FVector2 Center;
		FVector2 Extents;
	};

	struct Node
	{
		Node() = default;
		Node(const FVector2& aabb_min, const FVector2& aabb_max, int probeIndex, FVector3& probePos) : aabb(aabb_min, aabb_max), probeIndex(probeIndex), probePos(probePos) {}
		Node(const FVector2& aabb_min, const FVector2& aabb_max, int left, int right) : aabb(aabb_min, aabb_max), left(left), right(right) {}

		bool IsLeaf() const { return probeIndex == 0; }

		BBox aabb;
		int left = -1;
		int right = -1;
		int probeIndex = 0;
		FVector3 probePos;
	};

	std::vector<Node> Nodes;
	int Root = 0;

	struct
	{
		std::vector<int> leafs;
		std::vector<FVector3> centroids;
		std::vector<int> workbuffer;
	} Scratch;

	static bool OverlapAABB(const FVector2& center, float extent, const Node& node);
};
