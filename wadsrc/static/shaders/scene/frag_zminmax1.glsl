
layout(binding = 0) uniform sampler2D Texture;
layout(location = 0) out vec4 FragMinMax;

layout(push_constant) uniform ZMinMaxPushConstants
{
	float LinearizeDepthA;
	float LinearizeDepthB;
	float InverseDepthRangeA;
	float InverseDepthRangeB;
};

void main()
{
	ivec2 pos = ivec2(gl_FragCoord.xy) * 2;
	vec2 z0 = texelFetch(Texture, pos, 0).xy;
	vec2 z1 = texelFetch(Texture, pos + ivec2(1,0), 0).xy;
	vec2 z2 = texelFetch(Texture, pos + ivec2(0,1), 0).xy;
	vec2 z3 = texelFetch(Texture, pos + ivec2(1,1), 0).xy;
	vec2 zminmax = vec2(
		min(min(min(z0.x, z1.x), z2.x), z3.x),
		max(max(max(z0.y, z1.y), z2.y), z3.y));
	FragMinMax = vec4(zminmax, 0, 0);
}
