
#include <shaders/lightmap/binding_lightmapper.glsl>

layout(location = 0) in vec3 aPosition;
layout(location = 0) out vec3 worldpos;

layout(location = 1) out flat int InstanceIndex;

void main()
{
	vec3 WorldToLocal = constants[gl_InstanceIndex].WorldToLocal;
	float TextureSize = constants[gl_InstanceIndex].TextureSize;
	vec3 ProjLocalToU = constants[gl_InstanceIndex].ProjLocalToU;
	vec3 ProjLocalToV = constants[gl_InstanceIndex].ProjLocalToV;
	float TileX = constants[gl_InstanceIndex].TileX;
	float TileY = constants[gl_InstanceIndex].TileY;
	float TileWidth = constants[gl_InstanceIndex].TileWidth;
	float TileHeight = constants[gl_InstanceIndex].TileHeight;
	InstanceIndex = gl_InstanceIndex;

	worldpos = aPosition;

	// Project to position relative to tile
	vec3 localPos = aPosition - WorldToLocal;
	float x = dot(localPos, ProjLocalToU);
	float y = dot(localPos, ProjLocalToV);

	// Find the position in the output texture
	gl_Position = vec4(vec2(TileX + x, TileY + y) / TextureSize * 2.0 - 1.0, 0.0, 1.0);

	// Clip all surfaces to the edge of the tile (effectly we are applying a viewport/scissor to the tile)
	// Note: the tile has a 1px border around it that we also draw into
	gl_ClipDistance[0] = x + 1.0;
	gl_ClipDistance[1] = y + 1.0;
	gl_ClipDistance[2] = TileWidth + 1.0 - x;
	gl_ClipDistance[3] = TileHeight + 1.0 - y;
}
