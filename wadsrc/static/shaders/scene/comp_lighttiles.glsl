layout(local_size_x = 32, local_size_y = 32) in;
layout(set = 0, binding = 0, rg32f) readonly uniform image2D zminmax;

layout(set = 0, binding = 1) readonly buffer Lights
{
	vec4 lights[];
};

layout(set = 0, binding = 2) buffer Tiles
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
	const int lightSize = 4;
	int tileOffset = (tilePos.x + tilePos.y * zminmaxSize.x) * (1 + maxLights * lightSize);

	vec2 minmax = imageLoad(zminmax, tilePos).xy;
	float zmin = minmax.x;
	float zmax = minmax.y;

	int tileNormalCount = 1;
	int tileModulatedCount = 0;
	int tileSubtractiveCount = 0;

	// To do: cull and add lights

	tiles[tileOffset] = vec4(ivec4(
		0,
		tileNormalCount,
		tileNormalCount + tileModulatedCount,
		tileNormalCount + tileModulatedCount + tileSubtractiveCount));

	vec3 pos = vec3(-316.0, 64.0, 621.0);
	float radius = 200.0;
	vec3 color = vec3(1.0, 1.0, 1.0);
	float shadowIndex = -1025.0;
	vec3 spotDir = vec3(0.0);
	float lightType = 0.0;
	float spotInnerAngle = 0.0;
	float spotOuterAngle = 0.0;
	float softShadowRadius = 0.0;

	tiles[tileOffset + 1] = vec4(pos, radius);
	tiles[tileOffset + 2] = vec4(color, shadowIndex);
	tiles[tileOffset + 3] = vec4(spotDir, lightType);
	tiles[tileOffset + 4] = vec4(spotInnerAngle, spotOuterAngle, softShadowRadius, 0.0);
}
