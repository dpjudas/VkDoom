
#if defined(MULTISAMPLE)
layout(binding = 0) uniform sampler2DMS Texture;
#else
layout(binding = 0) uniform sampler2D Texture;
#endif

layout(location = 0) out vec4 FragMinMax;

layout(push_constant) uniform ZMinMaxPushConstants
{
	float LinearizeDepthA;
	float LinearizeDepthB;
	float InverseDepthRangeA;
	float InverseDepthRangeB;
};

float normalizeDepth(float depth);

void main()
{
#if defined(MULTISAMPLE)
	ivec2 size = textureSize(Texture) - 1;
#else
	ivec2 size = textureSize(Texture, 0) - 1;
#endif
	ivec2 pos = ivec2(gl_FragCoord.xy) * 2;
	float z0 = texelFetch(Texture, min(pos, size), 0).x;
	float z1 = texelFetch(Texture, min(pos + ivec2(1,0), size), 0).x;
	float z2 = texelFetch(Texture, min(pos + ivec2(0,1), size), 0).x;
	float z3 = texelFetch(Texture, min(pos + ivec2(1,1), size), 0).x;
	vec2 zminmax = vec2(
		min(min(min(z0, z1), z2), z3),
		max(max(max(z0, z1), z2), z3));
	FragMinMax = vec4(normalizeDepth(zminmax.x), normalizeDepth(zminmax.y), 0, 0);
}

float normalizeDepth(float depth)
{
	float normalizedDepth = clamp(InverseDepthRangeA * depth + InverseDepthRangeB, 0.0, 1.0);
	return 1.0 / (normalizedDepth * LinearizeDepthA + LinearizeDepthB);
}
