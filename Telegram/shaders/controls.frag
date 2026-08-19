#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D s_texture;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	float g_opacity;
};

void main() {
	vec4 result = texture(s_texture, v_texcoord);
	fragColor = result * g_opacity;
}
