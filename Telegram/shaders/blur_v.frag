#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D b_texture;

layout(std140, binding = 0) uniform BlurParams {
	float texelOffset;
};

const int radius = 15;
const int diameter = 2 * radius + 1;

void main() {
	vec4 sum = vec4(0.0);
	for (int i = 0; i < diameter; i++) {
		float offset = float(i - radius) * texelOffset;
		sum += texture(b_texture, v_texcoord + vec2(0.0, offset));
	}
	fragColor = sum / float(diameter);
}
