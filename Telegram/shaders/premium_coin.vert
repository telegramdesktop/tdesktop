#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(std140, binding = 0) uniform Shared {
	mat4 mvp;
	mat4 world;
	vec4 resolution;
	vec4 misc; // (time, night, alpha, _)
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vObjPos;

void main() {
	vNormal = inNormal;
	vUV = inUV;
	vObjPos = inPosition;
	gl_Position = mvp * vec4(inPosition, 1.0);
}
