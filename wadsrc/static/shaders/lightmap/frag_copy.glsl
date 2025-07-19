
layout(set = 0, binding = 0) uniform sampler2D Tex;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec3 WorldPos;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out uvec4 FragProbe;

void main()
{
	uint probeIndex = 0;

	FragColor = texture(Tex, TexCoord);
	FragProbe.x = probeIndex;
}
