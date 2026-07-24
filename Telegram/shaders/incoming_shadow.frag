#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D s_texture;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	vec3 shadow;
};

void main() {
	float fragY = viewport.y - gl_FragCoord.y;
	vec4 result = texture(s_texture, v_texcoord);
	float shadowCoord = shadow.y - fragY;
	float shadowValue = clamp(shadowCoord / shadow.x, 0.0, 1.0);
	float shadowShown = shadowValue * shadow.z;
	fragColor = vec4(min(result.rgb, vec3(1.0)) * (1.0 - shadowShown), result.a);
}
