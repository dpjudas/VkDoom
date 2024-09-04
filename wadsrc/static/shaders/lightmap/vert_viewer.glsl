
#include <shaders/lightmap/binding_viewer.glsl>

layout(location = 0) out vec3 FragRay;

vec2 positions[4] = vec2[](
	vec2(0.0, 0.0),
	vec2(1.0, 0.0),
	vec2(0.0, 1.0),
	vec2(1.0, 1.0)
);

void main()
{
	vec4 viewpos = vec4(positions[gl_VertexIndex] * 2.0 - 1.0, -1.0, 1.0);
	viewpos.x /= ProjX;
	viewpos.y = -viewpos.y / ProjY;
	FragRay = ((ViewToWorld * viewpos).xyz - CameraPos);

	gl_Position = vec4((positions[gl_VertexIndex] * 2.0 - 1.0) * vec2(1.0, -1.0), 1.0, 1.0);
}
