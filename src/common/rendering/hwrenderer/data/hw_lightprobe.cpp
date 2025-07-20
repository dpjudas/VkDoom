#include "hw_lightprobe.h"
#include "v_video.h"

void LightProbeIncrementalBuilder::Step(const TArray<LightProbe>& probes, std::function<void(int probeIndex, const LightProbe& probe)> renderScene)
{
	if (probes.size() == 0)
		return;

	if (lastIndex >= probes.size())
	{
		lastIndex = 0;
	}

	if (cubemapsAllocated != probes.size())
	{
		int newSegments = probes.size();
		int lastSegments = cubemapsAllocated;

		cubemapsAllocated = probes.size();

		lastIndex = 0;
		collected = 0;
		iterations = 0;

		screen->ResetLightProbes();
		return;
	}

	if (iterations >= 5)
		return; // We are done baking

	renderScene(lastIndex, probes[lastIndex]);
	lastIndex++;
	collected++;

	if (lastIndex >= probes.size())
	{
		if (collected == probes.size())
		{
			screen->EndLightProbePass();
		}
		collected = 0;
		iterations++;
	}
}

void LightProbeIncrementalBuilder::Full(const TArray<LightProbe>& probes, std::function<void(int probeIndex, const LightProbe& probe)> renderScene)
{
	if (lastIndex >= probes.size())
	{
		lastIndex = 0;

		if (!probes.size())
		{
			return;
		}
	}

	while (lastIndex < probes.size())
	{
		Step(probes, renderScene);
	}
}

/////////////////////////////////////////////////////////////////////////////

LightProbeAABBTree::LightProbeAABBTree(LevelMesh* mesh) : Mesh(mesh)
{
}

LightProbeAABBTree::~LightProbeAABBTree()
{
}

int LightProbeAABBTree::FindClosestProbe(FVector3 pos, float extent)
{
	if (Root == -1)
		return 0;

	float probeDistSqr = 0.0;
	int probeIndex = 0;
	Node* stack[64];
	int stackIndex = 0;
	stack[stackIndex++] = &Nodes[Root];
	do
	{
		Node* a = stack[--stackIndex];
		if (OverlapAABB(pos.XY(), extent, *a))
		{
			if (a->IsLeaf())
			{
				FVector3 probePos = a->probePos;
				FVector3 d = probePos - pos;
				float distSqr = d | d;
				if (probeIndex == 0 || probeDistSqr > distSqr)
				{
					probeIndex = a->probeIndex;
					probeDistSqr = distSqr;
				}
			}
			else
			{
				stack[stackIndex++] = &Nodes[a->right];
				stack[stackIndex++] = &Nodes[a->left];
			}
		}
	} while (stackIndex > 0);
	return probeIndex;
}

bool LightProbeAABBTree::OverlapAABB(const FVector2& center, float extent, const Node& node)
{
	float dx = center.X - node.aabb.Center.X;
	float px = extent + node.aabb.Extents.X - std::abs(dx);
	if (px < 0.0f)
		return false;
	float dy = center.Y - node.aabb.Center.Y;
	float py = extent + node.aabb.Extents.Y - std::abs(dy);
	if (py < 0.0f)
		return false;
	return true;
}

void LightProbeAABBTree::Update()
{
	//Create(Mesh->LightProbes);
	//Upload();
}

void LightProbeAABBTree::Upload()
{
}

void LightProbeAABBTree::Create(const TArray<LightProbe>& probes)
{
	Scratch.leafs.clear();
	Scratch.leafs.reserve(probes.size());
	Scratch.centroids.clear();
	Scratch.centroids.reserve(probes.size());
	for (int i = 0; i < probes.size(); i++)
	{
		Scratch.leafs.push_back(i);
		Scratch.centroids.push_back(FVector3(probes[i].position.XY(), 1.0f));
	}

	size_t neededbuffersize = probes.size() * 2;
	if (Scratch.workbuffer.size() < neededbuffersize)
		Scratch.workbuffer.resize(neededbuffersize);

	Nodes.clear();
	Root = Subdivide(Scratch.leafs.data(), (int)Scratch.leafs.size(), Scratch.centroids.data(), Scratch.workbuffer.data(), probes);
}

int LightProbeAABBTree::Subdivide(int* instances, int numInstances, const FVector3* centroids, int* workBuffer, const TArray<LightProbe>& probes)
{
	if (numInstances == 0)
		return -1;

	// Find bounding box and median of the instance centroids
	FVector2 median(0.0f, 0.0f);
	FVector2 min = probes[instances[0]].position.XY() - 1.0f;
	FVector2 max = probes[instances[0]].position.XY() + 1.0f;
	for (int i = 0; i < numInstances; i++)
	{
		FVector2 bboxmin = probes[instances[i]].position.XY() - 1.0f;
		FVector2 bboxmax = probes[instances[i]].position.XY() + 1.0f;

		min.X = std::min(min.X, bboxmin.X);
		min.Y = std::min(min.Y, bboxmin.Y);

		max.X = std::max(max.X, bboxmax.X);
		max.Y = std::max(max.Y, bboxmax.Y);

		median += centroids[instances[i]].XY();
	}
	median /= (float)numInstances;

	// For numerical stability
	min.X -= 0.1f;
	min.Y -= 0.1f;
	max.X += 0.1f;
	max.Y += 0.1f;

	if (numInstances == 1) // Leaf node
	{
		Nodes.push_back(Node(min, max, instances[0], probes[instances[0]].position));
		return (int)Nodes.size() - 1;
	}

	// Find the longest axis
	float axis_lengths[3] =
	{
		max.X - min.X,
		max.Y - min.Y
	};

	int axis_order[2] = { 0, 1 };
	std::sort(axis_order, axis_order + 2, [&](int a, int b) { return axis_lengths[a] > axis_lengths[b]; });

	// Try split at longest axis, then if that fails the next longest, and then the remaining one
	int left_count, right_count;
	FVector2 axis;
	for (int attempt = 0; attempt < 2; attempt++)
	{
		// Find the split plane for axis
		switch (axis_order[attempt])
		{
		default:
		case 0: axis = FVector2(1.0f, 0.0f); break;
		case 1: axis = FVector2(0.0f, 1.0f); break;
		}
		FVector3 plane(axis, -(median | axis)); // plane(axis, -dot(median, axis));

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
		left_index = Subdivide(instances, left_count, centroids, workBuffer, probes);
	if (right_count > 0)
		right_index = Subdivide(instances + left_count, right_count, centroids, workBuffer, probes);

	Nodes.push_back(Node(min, max, left_index, right_index));
	return (int)Nodes.size() - 1;
}
