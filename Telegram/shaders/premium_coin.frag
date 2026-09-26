#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vObjPos;

layout(location = 0) out vec4 fragColor;

layout(binding = 2) uniform sampler2D u_Texture;
layout(binding = 3) uniform sampler2D u_NormalMap;

layout(std140, binding = 0) uniform Shared {
	mat4 mvp;
	mat4 world;
	vec4 resolution;
	vec4 misc; // (time, night, alpha, _)
};

layout(std140, binding = 1) uniform Draw {
	vec4 drawParams; // (modelIndex, _, _, _)
};

// Four nested shells make up the coin, keyed by modelIndex:
//   0 coin_outer, 1 coin_inner, 2 coin_logo, 3 coin_stars.
void main() {
	float modelIndex = drawParams.x;
	float time = misc.x;
	bool night = (misc.y > 0.5);
	float alpha = misc.z;

	vec2 uv = vUV;
	if (modelIndex > 1.5 && modelIndex < 2.5) {
		uv *= 2.0;
		uv = fract(uv);
	}
	uv.x = 1.0 - uv.x;

	float diagonal = ((uv.x + uv.y) / 2.0 - 0.15) / 0.6;
	vec3 baseColor;
	if (modelIndex < 0.5) {
		baseColor = mix(
			vec3(0.95686, 0.47451, 0.93725),
			vec3(0.46274, 0.49411, 0.99600),
			diagonal);
	} else if (modelIndex > 2.5) {
		baseColor = mix(
			vec3(0.95686, 0.47451, 0.93725),
			vec3(0.46274, 0.49411, 0.99600),
			diagonal);
		baseColor = mix(baseColor, vec3(1.0), 0.3);
	} else if (modelIndex < 1.5) {
		baseColor = mix(
			vec3(0.67059, 0.25490, 0.80000),
			vec3(0.39608, 0.18824, 0.98039),
			diagonal);
	} else {
		baseColor = mix(
			vec3(0.91373, 0.62353, 0.99608),
			vec3(0.67451, 0.58824, 1.00000),
			clamp((uv.y - 0.1) / 0.8, 0.0, 1.0));
		baseColor = mix(
			baseColor,
			vec3(1.0),
			0.1 + 0.45 * texture(u_Texture, vUV).a);
		if (night) {
			baseColor = mix(baseColor, vec3(0.0), 0.06);
		}
	}

	vec3 pos = vObjPos / 100.0 + 0.5;
	vec3 norm = normalize(vec3(world * vec4(vNormal, 0.0)));

	vec3 flecksLightDir = normalize(vec3(0.5) - pos);
	vec3 flecksReflectDir = reflect(-flecksLightDir, norm);
	float flecksSpec = pow(
		max(dot(normalize(vec3(0.0) - pos), flecksReflectDir), 0.0),
		8.0);
	vec3 flecksNormal = normalize(texture(
		u_NormalMap,
		(uv * 1.3 + vec2(0.02, 0.06) * time) * 2.0).xyz * 2.0 - 1.0);
	norm += flecksSpec * flecksNormal;
	norm = normalize(norm);

	vec3 lightPos = vec3(-3.0, -3.0, 20.0);
	float diffuse = max(dot(norm, normalize(lightPos - pos)), 0.0);

	float spec = 0.0;
	lightPos = vec3(-3.0, -3.0, 0.5);
	spec += 2.0 * pow(
		max(dot(normalize(vec3(0.0) - pos),
			reflect(-normalize(lightPos - pos), norm)), 0.0),
		2.0);
	lightPos = vec3(-3.0, 0.5, 30.0);
	spec += ((modelIndex > 0.5 && modelIndex < 1.5) ? 1.5 : 0.5) * pow(
		max(dot(normalize(vec3(0.0) - pos),
			reflect(-normalize(lightPos - pos), norm)), 0.0),
		32.0);

	if (modelIndex >= 0.5) {
		spec *= 0.25;
	}

	vec3 color = baseColor;
	color *= 0.94 + 0.22 * diffuse;
	color = mix(color, vec3(1.0), spec);

	float a = alpha;
	fragColor = vec4(color * a, a);
}
