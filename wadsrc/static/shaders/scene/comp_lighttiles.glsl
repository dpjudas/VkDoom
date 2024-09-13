layout(local_size_x = 32, local_size_y = 32) in;
layout(binding = 0, rg32f) readonly uniform image2D zminmax;

layout(binding = 1) readonly buffer Lights
{
	vec4 lights[];
};

layout(binding = 1) buffer Tiles
{
	vec4 tiles[];
};

layout(push_constant) uniform PushConstants
{
	int normalLightCount;
	int modulatedLightCount;
	int subtractiveLightCount;
};

void main()
{
	ivec2 zminmaxSize = imageSize(zminmax);
	ivec2 tilePos = ivec2(gl_GlobalInvocationID.xy);
	if (tilePos.x >= zminmaxSize.x || tilePos.y >= zminmaxSize.y)
		return;

	const int maxLights = 16;
	const int lightSize = 3;
	int tileOffset = (tilePos.x + tilePos.y * zminmaxSize.x) * (1 + maxLights * lightSize);

	vec2 minmax = imageLoad(zminmax, tilePos).xy;
	float zmin = minmax.x;
	float zmax = minmax.y;

	int tileNormalCount = 0;
	int tileModulatedCount = 0;
	int tileSubtractiveCount = 0;

	// To do: cull and add lights

	tiles[tileOffset] = vec4(ivec4(tileNormalCount, tileModulatedCount, tileSubtractiveCount, 0));
}
