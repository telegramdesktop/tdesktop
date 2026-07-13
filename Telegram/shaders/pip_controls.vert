#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 v_texcoordIn;
layout(location = 2) in vec2 o_texcoordIn;

layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out vec2 o_texcoord;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	float g_opacity;
	float o_opacity;
};

void main() {
	v_texcoord = v_texcoordIn;
	o_texcoord = o_texcoordIn;
	gl_Position = vec4(
		vec2(-1.0, -1.0) + 2.0 * position / viewport,
		0.0,
		1.0);
}
