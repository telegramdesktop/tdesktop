#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(std140, binding = 0) uniform Shared {
	mat4 mvp;
	mat4 world;
	vec4 resolution;
	vec4 misc; // (time, night, alpha, _)
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 objPos;

void main() {
	vNormal = inNormal;
	objPos = inPosition;
	gl_Position = mvp * vec4(inPosition, 1.0);
}
