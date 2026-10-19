#version 450

// Port of Telegram Android's sparkleVertex.glsl (InteractiveDiamond2).

layout(location = 0) in vec4 inContours;
layout(location = 1) in vec4 inMaterial;

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

layout(std140, binding = 1) uniform Draw {
	vec4 draw; // base instance, _, _, _
};

layout(location = 0) out vec2 v_sourcePoint;
layout(location = 1) flat out float v_strength;
layout(location = 2) flat out uint v_layer;

void main() {
	uint instance = uint(gl_InstanceIndex) + uint(draw.x + 0.5);
	uint layer = uint(inMaterial.x);
	const float authoringToWorld = 2.0 / 447.9;
	vec3 local = u.anchors[2u * instance].xyz;
	vec3 normal = u.anchors[2u * instance + 1u].xyz;
	if (instance == 0u) {
		float c = cos(u.sparkleHalo.y), s = sin(u.sparkleHalo.y);
		local.xz = vec2(c * local.x + s * local.z, -s * local.x + c * local.z);
		normal.xz = vec2(c * normal.x + s * normal.z, -s * normal.x + c * normal.z);
	}
	if (instance != 0u) {
		local += normal * 0.008;
	}
	vec3 worldNormal = (u.model * vec4(normal, 0.0)).xyz;
	float pulse = pow(max(0.0, sin(u.parameters.x * 2.1 + float(instance) * 2.37 + 1.5)), 16.0);
	float front = (instance == 0u) ? u.sparkleHalo.w : smoothstep(0.15, 0.55, worldNormal.z);
	float strength = front * u.parameters.w;
	vec4 center = u.projection * u.model * vec4(local, 1.0);
	if (u.viewport.w > 0.0) {
		center = vec4(0.0, 0.0, 0.5, 1.0);
		strength = (instance == 0u) ? u.parameters.w : 0.0;
	}
	float morph = (instance == 0u) ? u.sparkleShape.y : 0.0;
	vec2 sourcePoint = mix(inContours.xy, inContours.zw, morph);
	float groupScale = 1.0;
	vec2 offset = vec2(0.0);
	if (layer == 0u) { groupScale = u.sparkleHalo.x; offset.y = 5.9; }
	if (layer == 1u) { groupScale = u.sparkleShape.w; offset.y = -0.8; }
	if (layer == 2u) { groupScale = u.sparkleShape.z; offset.y = -0.9; }
	if (layer == 3u) { groupScale = 0.309; offset = vec2(0.6, -0.4); }
	float mainEnvelope = (u.viewport.w > 0.0) ? 1.0 : u.sparkleHalo.w;
	float scale = (instance == 0u)
		? u.sparkleShape.x * mainEnvelope
		: (0.632 / 0.75) * pulse;
	vec2 point = (sourcePoint * groupScale + offset) * authoringToWorld * scale;
	// Correct the anchor with the gem, but keep the authored flare proportions.
	vec4 position = center + center.w * vec4(
		point.x * u.projection[0][0] / u.sparkleHalo.z,
		-point.y * u.projection[1][1],
		0.0,
		0.0);
	position.z = position.z * 2.0 - position.w;
	gl_Position = u.clipCorrection * position;
	v_sourcePoint = sourcePoint;
	v_strength = strength;
	v_layer = layer;
}
