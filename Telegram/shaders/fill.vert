#version 450

layout(location = 0) in vec2 position;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
};

void main() {
	gl_Position = vec4(
		vec2(-1.0, -1.0) + 2.0 * position / viewport,
		0.0,
		1.0);
}
