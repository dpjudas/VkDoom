
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

layout(push_constant) uniform PushConstants
{
	int additiveCount;
	int modulatedCount;
	int subtractiveCount;
	int padding;
	float rcpF;
	float rcpFDivAspect;
	vec2 twoRcpViewportSize;
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
	int outLightOffset = tileOffset + 1;

	int tileAdditiveCount = 0;
	int tileModulatedCount = 0;
	int tileSubtractiveCount = 0;

	if (isLightVisible(tile, vec3(-316.0, 64.0, 621.0), 200.0))
	{
		vec3 pos = vec3(-316.0, 64.0, 621.0);
		float radius = 200.0;
		vec3 color = vec3(1.0, 1.0, 1.0);
		float shadowIndex = -1025.0;
		vec3 spotDir = vec3(0.0);
		float lightType = 0.0;
		float spotInnerAngle = 0.0;
		float spotOuterAngle = 0.0;
		float softShadowRadius = 0.0;

		tileAdditiveCount++;
		tiles[outLightOffset++] = vec4(pos, radius);
		tiles[outLightOffset++] = vec4(color, shadowIndex);
		tiles[outLightOffset++] = vec4(spotDir, lightType);
		tiles[outLightOffset++] = vec4(spotInnerAngle, spotOuterAngle, softShadowRadius, 0.0);
	}

	tiles[tileOffset] = vec4(ivec4(
		0,
		tileAdditiveCount,
		tileAdditiveCount + tileModulatedCount,
		tileAdditiveCount + tileModulatedCount + tileSubtractiveCount));
}

bool isLightVisible(Tile tile, vec3 lightPos, float lightRadius)
{
/*
	// aabb/sphere test for the light
	vec3 e = max(tile.aabbMin - lightPos, 0.0f) + max(lightPos - tile.aabbMax, 0.0f);
	return dot(e, e) <= lightRadius * lightRadius;
*/
	return true;
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
	//	float f = 1.0f / tan(fovY * 0.5f);
	//	float rcpF = 1.0f / f;
	//	float rcpFDivAspect = 1.0f / (f / aspect);
	vec2 normalized = vec2(pos * twoRcpViewportSize) - 1.0;
	return vec3(normalized.x * rcpFDivAspect, normalized.y * rcpF, 1.0f);
}
