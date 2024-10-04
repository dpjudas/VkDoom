
layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0, rgba16f) writeonly uniform image2D colorBuffer;
layout(binding = 1) uniform samplerCube EnvironmentMap;

layout(push_constant) uniform PushConstants
{
	vec4 topLeft;
	vec4 bottomRight;
	float roughness;
	float padding1, padding2, padding3;
};

const float PI = 3.14159265359;

float RadicalInverse_VdC(uint bits);
vec2 Hammersley(uint i, uint N);
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness);

void main()
{
	uvec2 colorBufferSize = imageSize(colorBuffer);
	if (gl_GlobalInvocationID.x >= colorBufferSize.x || gl_GlobalInvocationID.y >= colorBufferSize.y)
		return;

	vec2 uv = (vec2(gl_GlobalInvocationID.xy) + 0.5) / vec2(colorBufferSize);
	vec3 FragPos = mix(topLeft.xyz, bottomRight.xyz, uv);

	vec3 normal = normalize(FragPos);

	const int SAMPLE_COUNT = 1024;

	float totalWeight = 0.0;
	vec4 prefilteredColor = vec4(0.0);

	for (int i = 0; i < SAMPLE_COUNT; i++)
	{
		vec2 Xi = Hammersley(uint(i), uint(SAMPLE_COUNT));
		vec3 H = ImportanceSampleGGX(Xi, normal, roughness);
		vec3 L = normalize(2.0 * dot(normal, H) * H - normal);

		float NdotL = clamp(dot(normal, L), 0.0, 1.0);
		if(NdotL > 0.0)
		{
			prefilteredColor += texture(EnvironmentMap, L) * NdotL;
			totalWeight += NdotL;
		}
	}

	prefilteredColor = prefilteredColor / totalWeight;

	imageStore(colorBuffer, ivec2(gl_GlobalInvocationID.xy), prefilteredColor);
}

float RadicalInverse_VdC(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}
vec2 Hammersley(uint i, uint N)
{
	return vec2(float(i)/float(N), RadicalInverse_VdC(i));
}

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
	float a = roughness*roughness;

	float phi = 2.0 * PI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta*cosTheta);

	// from spherical coordinates to cartesian coordinates
	vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

	// from tangent-space vector to world-space sample vector
	vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(up, N));
	vec3 bitangent = cross(N, tangent);

	vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
	return normalize(sampleVec);
}
