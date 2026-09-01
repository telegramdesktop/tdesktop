#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D b_texture;

layout(std140, binding = 0) uniform BlurParams {
	float texelOffset;
};

const vec3 satLuminanceWeighting = vec3(0.2126, 0.7152, 0.0722);
const vec2 offsets = vec2(1.0, 0.0);
const int radius = 15;
const int diameter = 2 * radius + 1;
// Triangle weights, they sum up to (radius + 1) * (radius + 1).
const float weightSum = float((radius + 1) * (radius + 1));

void main() {
	vec4 accumulated = vec4(0.0);
	for (int i = 0; i < diameter; i++) {
		float stepOffset = float(i - radius) * texelOffset;
		vec2 offset = vec2(stepOffset) * offsets;
		vec4 sampled = texture(b_texture, v_texcoord + offset);
		float fradius = float(radius);
		float boxWeight = fradius + 1.0 - abs(float(i) - fradius);
		accumulated += sampled * boxWeight;
	}
	vec3 blurred = accumulated.rgb / weightSum;
	float satLuminance = dot(blurred, satLuminanceWeighting);
	vec3 mixinColor = vec3(satLuminance);
	fragColor = vec4(clamp(mix(mixinColor, blurred, 1.1), 0.0, 1.0), 1.0);
}
