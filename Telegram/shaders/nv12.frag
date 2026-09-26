#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D y_texture;
layout(binding = 2) uniform sampler2D uv_texture;

void main() {
	float y = texture(y_texture, v_texcoord).r - 0.0625;
	vec2 uv = texture(uv_texture, v_texcoord).rg - vec2(0.5, 0.5);
	float u = uv.x;
	float v = uv.y;
	fragColor = vec4(
		1.164 * y + 1.596 * v,
		1.164 * y - 0.392 * u - 0.813 * v,
		1.164 * y + 2.017 * u,
		1.0);
}
