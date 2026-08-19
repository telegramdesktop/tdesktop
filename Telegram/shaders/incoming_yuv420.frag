#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D y_texture;
layout(binding = 2) uniform sampler2D u_texture;
layout(binding = 3) uniform sampler2D v_texture;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	vec3 shadow;
};

void main() {
	float fragY = viewport.y - gl_FragCoord.y;
	float y = texture(y_texture, v_texcoord).r - 0.0625;
	float u = texture(u_texture, v_texcoord).r - 0.5;
	float v = texture(v_texture, v_texcoord).r - 0.5;
	vec4 result = vec4(
		1.164 * y + 1.596 * v,
		1.164 * y - 0.392 * u - 0.813 * v,
		1.164 * y + 2.017 * u,
		1.0);
	float shadowCoord = shadow.y - fragY;
	float shadowValue = clamp(shadowCoord / shadow.x, 0.0, 1.0);
	float shadowShown = shadowValue * shadow.z;
	fragColor = vec4(min(result.rgb, vec3(1.0)) * (1.0 - shadowShown), result.a);
}
