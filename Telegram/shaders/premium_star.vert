#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vObjPos;

layout(std140, binding = 0) uniform Params {
	mat4 mvp;
	mat4 world;
	vec4 grad1;
	vec4 grad2;
	vec4 params;
	vec4 extra;
};

void main() {
	vNormal = inNormal;
	vUV = inUV;
	vObjPos = inPosition;
	gl_Position = mvp * vec4(inPosition, 1.0);
}
