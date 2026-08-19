#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 v_texcoordIn;

layout(location = 0) out vec2 v_texcoord;

void main() {
	v_texcoord = v_texcoordIn;
	gl_Position = vec4(position, 0.0, 1.0);
}
