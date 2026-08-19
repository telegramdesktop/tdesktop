#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D b_texture;

layout(std140, binding = 0) uniform BlurParams {
	float texelOffset;
};

const vec3 satLuminanceWeighting = vec3(0.2126, 0.7152, 0.0722);
const int radius = 15;
const int diameter = 2 * radius + 1;

void main() {
	vec4 sum = vec4(0.0);
	for (int i = 0; i < diameter; i++) {
		float offset = float(i - radius) * texelOffset;
		vec4 sample_ = texture(b_texture, v_texcoord + vec2(offset, 0.0));
		float luminance = dot(sample_.rgb, satLuminanceWeighting);
		sample_.rgb = mix(vec3(luminance), sample_.rgb, 0.65);
		sum += sample_;
	}
	fragColor = sum / float(diameter);
}
