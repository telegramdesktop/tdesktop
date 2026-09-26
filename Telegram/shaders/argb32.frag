#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D s_texture;

void main() {
	fragColor = texture(s_texture, v_texcoord);
}
