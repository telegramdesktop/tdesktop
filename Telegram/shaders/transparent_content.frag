#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D s_texture;
layout(binding = 2) uniform sampler2D f_texture;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	vec4 shadowTopRect;
	vec4 shadowBottomSkipOpacityFullFade;
	vec4 transparentBg;
	vec4 transparentFg;
	float transparentSize;
};

void main() {
	vec2 fragCoord = vec2(gl_FragCoord.x, viewport.y - gl_FragCoord.y);
	vec4 result = texture(s_texture, v_texcoord);

	vec2 checkboardLadder = floor(fragCoord / transparentSize);
	float checkboard = mod(checkboardLadder.x + checkboardLadder.y, 2.0);
	vec4 bg = mix(transparentBg, transparentFg, checkboard);
	result = vec4(result.rgb * result.a + bg.rgb * (1.0 - result.a), 1.0);

	float topHeight = shadowTopRect.w;
	float bottomHeight = shadowBottomSkipOpacityFullFade.x;
	float bottomSkip = shadowBottomSkipOpacityFullFade.y;
	float opacity = shadowBottomSkipOpacityFullFade.z;
	float fullFade = shadowBottomSkipOpacityFullFade.w;
	float viewportHeight = shadowTopRect.y + topHeight;
	float fullHeight = topHeight + bottomHeight;
	float topY = min(
		(viewportHeight - fragCoord.y) / fullHeight,
		topHeight / fullHeight);
	float topX = (fragCoord.x - shadowTopRect.x) / shadowTopRect.z;
	vec4 fadeTop = texture(f_texture, vec2(topX, topY)) * opacity;
	float bottomY = max(bottomSkip + fullHeight - fragCoord.y, topHeight)
		/ fullHeight;
	vec4 fadeBottom = texture(f_texture, vec2(0.5, bottomY)) * opacity;
	float fade = min((1.0 - fadeTop.a) * (1.0 - fadeBottom.a), fullFade);
	result.rgb = result.rgb * fade;

	fragColor = result;
}
