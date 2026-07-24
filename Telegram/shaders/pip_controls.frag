#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 1) in vec2 o_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D s_texture;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	float g_opacity;
	float o_opacity;
};

void main() {
	vec4 result = texture(s_texture, v_texcoord);
	vec4 over = texture(s_texture, o_texcoord);
	result = result * (1.0 - o_opacity) + over * o_opacity;
	fragColor = result * g_opacity;
}
