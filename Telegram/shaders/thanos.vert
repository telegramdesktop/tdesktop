#version 450

layout(location = 0) in vec2 inQuadPos;

layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out float v_alpha;

layout(binding = 2) uniform sampler2D particleStateTex;

layout(std140, binding = 0) uniform Params {
	vec4 rect;
	vec2 size;
	uvec2 particleResolution;
	vec4 scale;
};

void main() {
	uint particleId = uint(gl_InstanceIndex);
	uint pX = particleId % particleResolution.x;
	uint pY = particleId / particleResolution.x;

	vec4 state = texelFetch(
		particleStateTex,
		ivec2(int(pX), int(pY)),
		0);
	vec2 inOffset = state.xy;
	float inLifetime = state.z;

	vec2 particleSize = size / vec2(particleResolution);

	vec2 topLeft = vec2(float(pX) * particleSize.x, float(pY) * particleSize.y);
	v_texcoord = (topLeft + inQuadPos * particleSize) / size;

	topLeft += inOffset;
	float scaleFactor = scale.x;
	vec2 center = topLeft + (particleSize * 0.5);
	vec2 position
		= center + ((inQuadPos - vec2(0.5)) * particleSize * scaleFactor);

	vec2 ndc;
	ndc.x = rect.x + position.x / size.x * rect.z;
	ndc.y = rect.y + position.y / size.y * rect.w;
	ndc.x = -1.0 + ndc.x * 2.0;
	ndc.y = -1.0 + ndc.y * 2.0;

	gl_Position = vec4(ndc, 0.0, 1.0);

	v_alpha = clamp(inLifetime / 0.6, 0.0, 1.0) * scale.z;
}
