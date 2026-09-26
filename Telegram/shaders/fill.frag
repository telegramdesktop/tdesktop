#version 450

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	vec4 s_color;
};

void main() {
	fragColor = s_color;
}
