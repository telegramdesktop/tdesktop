#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D s_texture;
layout(binding = 2) uniform sampler2D h_texture;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	// 1.0 when gl_FragCoord.y counts from the bottom (OpenGL).
	float fragCoordYUp;
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
	// Nine-slice sampling of the shadow atlas, the same mapping the OpenGL
	// renderer uses: the corners are taken pixel to pixel, the edges are
	// sampled at the middle of the atlas and stretch.
	vec2 origin = roundRect.xy - vec2(h_extend.x, h_extend.w);
	vec2 size = roundRect.zw
		+ vec2(h_extend.x + h_extend.z, h_extend.y + h_extend.w);
	vec2 corner = h_components.xy;
	vec2 inside = fc - origin;
	vec2 fromOther = inside + h_size - size;
	vec2 uv = vec2(
		(inside.x < corner.x)
			? inside.x
			: (fromOther.x > h_size.x - corner.x)
				? fromOther.x
				: (0.5 * h_size.x),
		(inside.y < corner.y)
			? inside.y
			: (fromOther.y > h_size.y - corner.y)
				? fromOther.y
				: (0.5 * h_size.y));
	return texture(h_texture, uv / h_size).a;
}

void main() {
	float fragY = (fragCoordYUp > 0.0)
		? gl_FragCoord.y
		: (viewport.y - gl_FragCoord.y);
	vec2 fc = vec2(gl_FragCoord.x, fragY);
	vec4 result = texture(s_texture, v_texcoord);
	result = result * (1.0 - fadeColor.a) + fadeColor;
	float corner = roundedCorner(fc);
	float shadowValue = shadow(fc);
	fragColor = result * corner + vec4(0.0, 0.0, 0.0, shadowValue) * (1.0 - corner);
}
