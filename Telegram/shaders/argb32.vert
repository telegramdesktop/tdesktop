#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 v_texcoordIn;

layout(location = 0) out vec2 v_texcoord;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
};

void main() {
	v_texcoord = v_texcoordIn;
	gl_Position = vec4(
		vec2(-1.0, -1.0) + 2.0 * position / viewport,
		0.0,
		1.0);
}
