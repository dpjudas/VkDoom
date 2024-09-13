
layout(binding = 0) uniform sampler2D Texture;
layout(location = 0) out vec4 FragMinMax;

void main()
{
	ivec2 size = textureSize(Texture, 0) - 1;
	ivec2 pos = ivec2(gl_FragCoord.xy) * 2;
	float z0 = texelFetch(Texture, min(pos, size), 0).w;
	float z1 = texelFetch(Texture, min(pos + ivec2(1,0), size), 0).w;
	float z2 = texelFetch(Texture, min(pos + ivec2(0,1), size), 0).w;
	float z3 = texelFetch(Texture, min(pos + ivec2(1,1), size), 0).w;
	vec2 zminmax = vec2(
		min(min(min(z0, z1), z2), z3),
		max(max(max(z0, z1), z2), z3));
	FragMinMax = vec4(zminmax, 0, 0);
}
