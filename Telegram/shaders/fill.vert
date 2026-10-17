#version 450

layout(location = 0) in vec2 position;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	float _pad0;
	// -1.0 when the NDC Y axis points down (Vulkan), 1.0 otherwise.
	float flipY;
};

void main() {
	vec2 ndc = vec2(-1.0, -1.0) + 2.0 * position / viewport;
	gl_Position = vec4(ndc.x, ndc.y * flipY, 0.0, 1.0);
}
