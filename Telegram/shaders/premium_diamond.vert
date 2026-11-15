#version 450

// Port of Telegram Android's diamondVertex.glsl (InteractiveDiamond2).

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inNormal;
layout(location = 2) in vec4 inSurface;

layout(std140, binding = 0) uniform Uniforms {
	mat4 model;
	mat4 projection; // Metal-style clip space, depth in [0, w].
	mat4 inverseModel;
	mat4 clipCorrection;
	vec4 parameters; // time, refraction, brightness, sparkles
	vec4 viewport; // width, height, optical plane count, centered sparkle
	vec4 crownGradient;
	vec4 pavilionGradient;
	vec4 lightSweep; // diagonal position, environment phase, transmission, _
	vec4 crownSweep;
	vec4 rightCrownSweep;
	vec4 leftCrownSweep;
	vec4 pavilionSweep;
	vec4 rightPavilionSweep;
	vec4 leftPavilionSweep;
	vec4 sparkleShape; // main layer scale, contour morph, core scale, star glow scale
	vec4 sparkleHalo; // circular glow scale, main face rotation, horizontal correction, visibility
	vec4 facetProjection; // reference pitch cosine/sine, source units, projected top
	vec4 composition; // opacity, _, _, _
	vec4 planes[17];
	vec4 anchors[16]; // (position, normal) pairs
} u;

layout(location = 0) out vec3 v_localPosition;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec3 v_worldPosition;
layout(location = 3) out vec3 v_facetWeights;

void main() {
	vec4 world = u.model * inPosition;
	vec4 position = u.projection * world;
	position.z = position.z * 2.0 - position.w;
	gl_Position = u.clipCorrection * position;
	v_localPosition = inPosition.xyz;
	v_normal = normalize((u.model * inNormal).xyz);
	v_worldPosition = world.xyz;
	// Table, crown, pavilion; interpolated across rounded edges.
	v_facetWeights = (inSurface.z == 3.0)
		? vec3(inSurface.xy, 1.0 - inSurface.x - inSurface.y)
		: vec3(
			inSurface.z == 0.0 ? 1.0 : 0.0,
			inSurface.z == 1.0 ? 1.0 : 0.0,
			inSurface.z == 2.0 ? 1.0 : 0.0);
}
