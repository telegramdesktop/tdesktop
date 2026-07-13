#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 v_texcoordIn;
layout(location = 2) in vec2 b_texcoordIn;

layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out vec2 b_texcoord;
layout(location = 2) out vec2 v_position;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	vec4 frameBg;
	vec4 shadow;
	float paused;
	vec4 roundRect;
	vec2 radiusOutline;
	vec4 roundBg;
	vec4 outlineFg;
};

void main() {
	v_texcoord = v_texcoordIn;
	b_texcoord = b_texcoordIn;
	v_position = position;
	gl_Position = vec4(
		vec2(-1.0, -1.0) + 2.0 * position / viewport,
		0.0,
		1.0);
}
