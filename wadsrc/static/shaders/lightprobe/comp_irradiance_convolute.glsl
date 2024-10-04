
layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0, rgba16f) writeonly uniform image2D colorBuffer;
layout(binding = 1) uniform samplerCube EnvironmentMap;

layout(push_constant) uniform PushConstants
{
	vec4 topLeft;
	vec4 bottomRight;
};

const float PI = 3.14159265359;

void main()
{
	uvec2 colorBufferSize = imageSize(colorBuffer);
	if (gl_GlobalInvocationID.x >= colorBufferSize.x || gl_GlobalInvocationID.y >= colorBufferSize.y)
		return;

	vec2 uv = (vec2(gl_GlobalInvocationID.xy) + 0.5) / vec2(colorBufferSize);
	vec3 FragPos = mix(topLeft.xyz, bottomRight.xyz, uv);

	vec3 normal = normalize(FragPos);
	vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), normal));
	vec3 up = normalize(cross(normal, right));

	float sampleDelta = 0.025;
	float sampleCount = 0.0;

	vec4 irradiance = vec4(0.0);

	for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
	{
		for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
		{
			// spherical to cartesian (in tangent space)
			vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));

			// tangent space to world
			vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal;

			irradiance += texture(EnvironmentMap, sampleVec) * cos(theta) * sin(theta);
			sampleCount++;
		}
	}

	irradiance = PI * irradiance * (1.0 / sampleCount);

	imageStore(colorBuffer, ivec2(gl_GlobalInvocationID.xy), irradiance);
}
