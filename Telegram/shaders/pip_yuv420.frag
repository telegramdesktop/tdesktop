#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D y_texture;
layout(binding = 2) uniform sampler2D u_texture;
layout(binding = 3) uniform sampler2D v_texture;
layout(binding = 4) uniform sampler2D h_texture;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	vec4 roundRect;
	float roundRadius;
	vec4 fadeColor;
	vec2 h_size;
	vec4 h_extend;
	vec4 h_components;
};

float roundedCorner(vec2 fc) {
	vec2 rectHalf = roundRect.zw / 2.0;
	vec2 rectCenter = roundRect.xy + rectHalf;
	vec2 fromRectCenter = abs(fc - rectCenter);
	vec2 vectorRadius = vec2(roundRadius + 0.5);
	vec2 fromCenterWithRadius = fromRectCenter + vectorRadius;
	vec2 fromRoundingCenter = max(fromCenterWithRadius, rectHalf) - rectHalf;
	float rounded = length(fromRoundingCenter) - roundRadius;
	return 1.0 - smoothstep(0.0, 1.0, rounded);
}

float shadow(vec2 fc) {
	vec2 texcoord = fc - roundRect.xy + h_extend.xy;
	vec2 total = roundRect.zw + h_extend.xy + h_extend.zw;
	vec2 dividedTexcoord = texcoord / total;
	float left = h_components.x / h_size.x;
	float right = 1.0 - h_components.y / h_size.x;
	float top = h_components.z / h_size.y;
	float bottom = 1.0 - h_components.w / h_size.y;
	float sampleX = dividedTexcoord.x < left
		? dividedTexcoord.x * total.x / h_size.x
		: dividedTexcoord.x > right
			? 1.0 - (1.0 - dividedTexcoord.x) * total.x / h_size.x
			: mix(left, right, (dividedTexcoord.x - left) / (right - left) * (h_size.x - h_components.x - h_components.y) / h_size.x + h_components.x / h_size.x);
	float sampleY = dividedTexcoord.y < top
		? dividedTexcoord.y * total.y / h_size.y
		: dividedTexcoord.y > bottom
			? 1.0 - (1.0 - dividedTexcoord.y) * total.y / h_size.y
			: mix(top, bottom, (dividedTexcoord.y - top) / (bottom - top) * (h_size.y - h_components.z - h_components.w) / h_size.y + h_components.z / h_size.y);
	return texture(h_texture, vec2(
		clamp(sampleX, 0.0, 1.0),
		clamp(sampleY, 0.0, 1.0))).a;
}

void main() {
	vec2 fc = vec2(gl_FragCoord.x, viewport.y - gl_FragCoord.y);
	float y = texture(y_texture, v_texcoord).r - 0.0625;
	float u = texture(u_texture, v_texcoord).r - 0.5;
	float v = texture(v_texture, v_texcoord).r - 0.5;
	vec4 result = vec4(
		1.164 * y + 1.596 * v,
		1.164 * y - 0.392 * u - 0.813 * v,
		1.164 * y + 2.017 * u,
		1.0);
	result = result * (1.0 - fadeColor.a) + fadeColor;
	float corner = roundedCorner(fc);
	float shadowValue = shadow(fc);
	fragColor = result * corner + vec4(0.0, 0.0, 0.0, shadowValue) * (1.0 - corner);
}
