#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 1) in vec2 b_texcoord;
layout(location = 2) in vec2 v_position;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D s_texture;
layout(binding = 2) uniform sampler2D b_texture;
layout(binding = 3) uniform sampler2D n_texture;

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

vec2 roundedCorner(vec2 fc) {
	vec2 rectHalf = roundRect.zw / 2.0;
	vec2 rectCenter = roundRect.xy + rectHalf;
	vec2 fromRectCenter = abs(fc - rectCenter);
	vec2 vectorRadius = radiusOutline.xx + vec2(0.5);
	vec2 fromCenterWithRadius = fromRectCenter + vectorRadius;
	vec2 fromRoundingCenter = max(fromCenterWithRadius, rectHalf) - rectHalf;
	float rounded = length(fromRoundingCenter) - radiusOutline.x;
	float outline = rounded + radiusOutline.y;
	return vec2(
		1.0 - smoothstep(0.0, 1.0, rounded),
		1.0 - (smoothstep(0.0, 1.0, outline) * outlineFg.a));
}

float insideTexture() {
	vec2 textureHalf = vec2(0.5, 0.5);
	vec2 fromTextureCenter = abs(v_texcoord - textureHalf);
	vec2 fromTextureEdge = max(fromTextureCenter, textureHalf) - textureHalf;
	float outsideCheck = dot(fromTextureEdge, fromTextureEdge);
	return step(outsideCheck, 0.0);
}

vec4 background() {
	vec4 blur = texture(b_texture, b_texcoord);
	float blurOpacity = shadow.w;
	return mix(frameBg, blur, blurOpacity);
}

void main() {
	vec2 fc = v_position;

	float inside = insideTexture() * (1.0 - paused);
	vec4 result;
	float backgroundOpacity = shadow.w;
	vec4 mainColor = texture(s_texture, v_texcoord);
	result = mainColor * inside
		+ (1.0 - inside) * (backgroundOpacity * background()
			+ (1.0 - backgroundOpacity) * frameBg);

	float shadowCoord = fc.y - roundRect.y;
	float shadowValue = max(1.0 - (shadowCoord / shadow.x), 0.0);
	float shadowShown = max(shadowValue * shadow.y, paused) * shadow.z;
	result = vec4(result.rgb * (1.0 - shadowShown), result.a);

	float noiseValue = texture(n_texture, fc / vec2(256.0)).r;
	result.rgb += (noiseValue - 0.5) * 0.002;

	vec2 roundOutline = roundedCorner(fc);
	result = result * roundOutline.y
		+ vec4(outlineFg.rgb, 1.0) * (1.0 - roundOutline.y);
	result = result * roundOutline.x + roundBg * (1.0 - roundOutline.x);
	fragColor = result;
}
