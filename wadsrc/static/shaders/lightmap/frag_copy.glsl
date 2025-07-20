
layout(set = 0, binding = 0) uniform sampler2D Tex;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec3 WorldPos;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out uvec4 FragProbe;

#if 1

uint findClosestProbe(vec3 pos, float size)
{
	return 0;
}

#else

struct ProbeNode
{
	vec2 center;
	vec2 extents;
	int left;
	int right;
	uint probeIndex;
	int padding0;
	vec3 probePos;
	float padding1;
};

layout(std430, set = 0, binding = 1) buffer readonly ProbeBuffer
{
	int probeNodeRoot;
	int probebufferPadding1;
	int probebufferPadding2;
	int probebufferPadding3;
	ProbeNode probeNodes[];
};

bool isProbeNodeLeaf(int nodeIndex)
{
	return probeNodes[nodeIndex].probeIndex != 0;
}

bool overlapAABB(vec2 center, vec2 extents, int nodeIndex)
{
	vec2 d = center - probeNodes[nodeIndex].center;
	vec2 p = extents + probeNodes[nodeIndex].extents - abs(d);
	return p.x >= 0.0 && p.y >= 0.0;
}

uint findClosestProbe(vec3 pos, float size)
{
	float probeDistSqr = 0.0;
	uint probeIndex = 0;
	int stack[64];
	int stackIndex = 0;
	stack[stackIndex++] = probeNodeRoot;
	do
	{
		int a = stack[--stackIndex];
		if (overlapAABB(pos, vec2(size, size), a))
		{
			if (isProbeNodeLeaf(a))
			{
				vec3 probePos = probeNodes[a].probePos;
				vec3 d = probePos - pos;
				float distSqr = dot(d, d);
				if (probeIndex == 0 || probeDistSqr > distSqr)
				{
					probeIndex = probeNodes[a].probeIndex;
					probeDistSqr = distSqr;
				}
			}
			else
			{
				stack[stackIndex++] = nodes[a].right;
				stack[stackIndex++] = nodes[a].left;
			}
		}
	} while (stackIndex > 0);
	return probeIndex;
}

#endif

void main()
{
	uint probeIndex = findClosestProbe(WorldPos, 512.0);

	FragColor = texture(Tex, TexCoord);
	FragProbe.x = probeIndex;
}
