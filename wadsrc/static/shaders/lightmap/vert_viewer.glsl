
#include <shaders/lightmap/binding_viewer.glsl>

vec2 positions[4] = vec2[](
	vec2(0.0, 0.0),
	vec2(1.0, 0.0),
	vec2(0.0, 1.0),
	vec2(1.0, 1.0)
);

void main()
{
	gl_Position = vec4((positions[gl_VertexIndex] * 2.0 - 1.0) * vec2(1.0, -1.0), 1.0, 1.0);
}
