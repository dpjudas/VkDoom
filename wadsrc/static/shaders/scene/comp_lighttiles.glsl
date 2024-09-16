
#define TILE_SIZE 64

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

layout(push_constant) uniform LightTilesPushConstants
{
	vec2 posToViewA;
	vec2 posToViewB;
	vec2 viewportPos;
	vec2 padding1;
	mat4 worldToView;
};

struct Tile
{
	vec3 aabbMin;
	vec3 aabbMax;
};

bool isLightVisible(Tile tile, vec3 lightPos, float lightRadius);
Tile findTileFrustum(float viewZNear, float viewZFar, uint tileX, uint tileY);
vec3 unprojectDirection(vec2 pos);

void main()
{
	ivec2 zminmaxSize = imageSize(zminmax);
	ivec2 tilePos = ivec2(gl_GlobalInvocationID.xy);
	if (tilePos.x >= zminmaxSize.x || tilePos.y >= zminmaxSize.y)
		return;

	vec2 minmax = imageLoad(zminmax, tilePos).xy;
	float zmin = minmax.x;
	float zmax = minmax.y;
	Tile tile = findTileFrustum(zmin, zmax, tilePos.x, tilePos.y);

	const int maxLights = 16;
	const int lightSize = 4;
	int tileOffset = (tilePos.x + tilePos.y * zminmaxSize.x) * (1 + maxLights * lightSize);

	ivec4 inRanges = ivec4(lights[0]) + ivec4(1);
	ivec4 outRanges = ivec4(0);

	int count = 0;
	int offset = tileOffset + 1;
	for (int i = inRanges.x; i < inRanges.y; i += 4)
	{
		vec4 inLight = lights[i];
		vec3 pos = (worldToView * vec4(inLight.xyz, 1.0)).xyz;
		float radius = inLight.w;
		if (isLightVisible(tile, pos, radius))
		{
			tiles[offset++] = inLight;
			tiles[offset++] = lights[i + 1];
			tiles[offset++] = lights[i + 2];
			tiles[offset++] = lights[i + 3];
			count += 4;
		}
	}
	outRanges.y = count;

	for (int i = inRanges.y; i < inRanges.z; i += 4)
	{
		vec4 inLight = lights[i];
		vec3 pos = (worldToView * vec4(inLight.xyz, 1.0)).xyz;
		float radius = inLight.w;
		if (isLightVisible(tile, pos, radius))
		{
			tiles[offset++] = inLight;
			tiles[offset++] = lights[i + 1];
			tiles[offset++] = lights[i + 2];
			tiles[offset++] = lights[i + 3];
			count += 4;
		}
	}
	outRanges.z = count;

	for (int i = inRanges.z; i < inRanges.w; i += 4)
	{
		vec4 inLight = lights[i];
		vec3 pos = (worldToView * vec4(inLight.xyz, 1.0)).xyz;
		float radius = inLight.w;
		if (isLightVisible(tile, pos, radius))
		{
			tiles[offset++] = inLight;
			tiles[offset++] = lights[i + 1];
			tiles[offset++] = lights[i + 2];
			tiles[offset++] = lights[i + 3];
			count += 4;
		}
	}
	outRanges.w = count;

	tiles[tileOffset] = vec4(outRanges);
}

bool isLightVisible(Tile tile, vec3 lightPos, float lightRadius)
{
	// Negative Z go into the screen, but zminmax is positive Z
	lightPos.z = -lightPos.z;

	// aabb/sphere test for the light
	vec3 e = max(tile.aabbMin - lightPos, 0.0f) + max(lightPos - tile.aabbMax, 0.0f);
	return dot(e, e) <= lightRadius * lightRadius;
}

Tile findTileFrustum(float viewZNear, float viewZFar, uint tileX, uint tileY)
{
	uint tileWidth = TILE_SIZE;
	uint tileHeight = TILE_SIZE;
	uint x = tileX * tileWidth;
	uint y = tileY * tileHeight;
	vec3 tl_direction = unprojectDirection(vec2(x, y));
	vec3 br_direction = unprojectDirection(vec2(x + tileWidth, y + tileHeight));
	vec3 front_tl = tl_direction * viewZNear;
	vec3 front_br = br_direction * viewZNear;
	vec3 back_tl = tl_direction * viewZFar;
	vec3 back_br = br_direction * viewZFar;
	Tile tile;
	tile.aabbMin = min(min(min(front_tl, front_br), back_tl), back_br);
	tile.aabbMax = max(max(max(front_tl, front_br), back_tl), back_br);
	return tile;
}

vec3 unprojectDirection(vec2 pos)
{
    return vec3(posToViewA * (pos - viewportPos) + posToViewB, 1.0f);
}
