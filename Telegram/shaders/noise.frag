#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

const float permTexUnit = 1.0 / 256.0;
const float permTexUnitHalf = 0.5 / 256.0;
const float grainsize = 1.3;
const float noiseCoordRotation = 1.425;
const vec2 dimensions = vec2(256.0, 256.0);

layout(binding = 1) uniform sampler2D p_texture;

vec4 rnm(vec2 tc) {
	float noise = sin(dot(tc, vec2(12.9898, 78.233))) * 43758.5453;
	float noiseR = fract(noise) * 2.0 - 1.0;
	float noiseG = fract(noise * 1.2154) * 2.0 - 1.0;
	float noiseB = fract(noise * 1.3453) * 2.0 - 1.0;
	float noiseA = fract(noise * 1.4651) * 2.0 - 1.0;
	return vec4(noiseR, noiseG, noiseB, noiseA);
}

float fade(float t) {
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float pnoise3D(vec3 p) {
	vec3 pi = permTexUnit * floor(p) + permTexUnitHalf;
	vec3 pf = fract(p);
	float perm00 = rnm(pi.xy).a;
	vec3 grad000 = rnm(vec2(perm00, pi.z)).rgb * 4.0 - 1.0;
	float n000 = dot(grad000, pf);
	vec3 grad001 = rnm(vec2(perm00, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
	float n001 = dot(grad001, pf - vec3(0.0, 0.0, 1.0));
	float perm01 = rnm(pi.xy + vec2(0.0, permTexUnit)).a;
	vec3 grad010 = rnm(vec2(perm01, pi.z)).rgb * 4.0 - 1.0;
	float n010 = dot(grad010, pf - vec3(0.0, 1.0, 0.0));
	vec3 grad011 = rnm(vec2(perm01, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
	float n011 = dot(grad011, pf - vec3(0.0, 1.0, 1.0));
	float perm10 = rnm(pi.xy + vec2(permTexUnit, 0.0)).a;
	vec3 grad100 = rnm(vec2(perm10, pi.z)).rgb * 4.0 - 1.0;
	float n100 = dot(grad100, pf - vec3(1.0, 0.0, 0.0));
	vec3 grad101 = rnm(vec2(perm10, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
	float n101 = dot(grad101, pf - vec3(1.0, 0.0, 1.0));
	float perm11 = rnm(pi.xy + vec2(permTexUnit, permTexUnit)).a;
	vec3 grad110 = rnm(vec2(perm11, pi.z)).rgb * 4.0 - 1.0;
	float n110 = dot(grad110, pf - vec3(1.0, 1.0, 0.0));
	vec3 grad111 = rnm(vec2(perm11, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
	float n111 = dot(grad111, pf - vec3(1.0, 1.0, 1.0));
	vec4 n_x = mix(
		vec4(n000, n001, n010, n011),
		vec4(n100, n101, n110, n111),
		fade(pf.x));
	vec2 n_xy = mix(n_x.xy, n_x.zw, fade(pf.y));
	float n_xyz = mix(n_xy.x, n_xy.y, fade(pf.z));
	return n_xyz;
}

vec2 rotateTexCoords(vec2 tc, float angle) {
	tc -= 0.5;
	float s = sin(angle);
	float c = cos(angle);
	tc = mat2(c, -s, s, c) * tc;
	tc += 0.5;
	return tc;
}

void main() {
	vec2 texCoord = v_texcoord * dimensions / grainsize;
	vec2 rotated = rotateTexCoords(texCoord, noiseCoordRotation);
	float noise = pnoise3D(vec3(rotated, 0.0));
	fragColor = vec4(noise * 0.5 + 0.5);
}
