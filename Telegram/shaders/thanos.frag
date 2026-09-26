#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 1) in float v_alpha;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D tex;

void main() {
	if (v_alpha <= 0.0) {
		discard;
	}
	vec4 color = texture(tex, vec2(v_texcoord.x, 1.0 - v_texcoord.y));
	fragColor = color * v_alpha;
}
