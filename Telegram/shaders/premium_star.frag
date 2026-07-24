#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vObjPos;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D u_Texture;
layout(binding = 2) uniform sampler2D u_NormalMap;

layout(std140, binding = 0) uniform Params {
	mat4 mvp;
	mat4 world;
	vec4 grad1;
	vec4 grad2;
	vec4 params;
	vec4 extra;
};

float goldenSpec(vec3 lightPos, vec3 pos, vec3 norm, vec3 eyeDir, float k) {
	vec3 refl = reflect(-normalize(lightPos - pos), norm);
	return clamp(k * pow(max(dot(eyeDir, refl), 0.0), 2.0), 0.0, 1.0);
}

vec4 goldenStar() {
	float f_xOffset = params.x;
	float alpha = extra.y;
	float white = extra.w;
	vec3 gradientColor1 = grad1.rgb;
	vec3 gradientColor2 = grad2.rgb;

	vec3 pos = vObjPos / 100.0 + 0.5;
	float specTexture = texture(u_Texture, vUV).y * clamp(vNormal.z, 0.0, 1.0);

	float gradientMix = clamp(
		distance(pos.xy, vec2(1.0, 1.0)) - 0.05 * specTexture,
		0.0,
		1.0);
	vec4 color = vec4(mix(gradientColor1, gradientColor2, gradientMix), 1.0);

	vec3 norm = normalize(vec3(world * vec4(vNormal, 0.0)));
	vec3 flecksNormal = normalize(1.0 - texture(
		u_NormalMap,
		(vUV + 0.7 * vec2(-f_xOffset, f_xOffset)) * 2.0).xyz);
	vec3 eyeDir = normalize(vec3(0.0) - pos);

	float diffuse = max(dot(norm, normalize(vec3(-3.0, -3.0, 20.0) - pos)), 0.0);

	float spec = 0.0;
	spec += specTexture * goldenSpec(vec3(-1.0, 0.7, 0.2), pos, norm, eyeDir, 2.0) / 6.0;
	spec += specTexture * goldenSpec(vec3(8.0, 0.7, 0.5), pos, norm, eyeDir, 2.0) / 6.0;
	spec += goldenSpec(vec3(-3.0, -3.0, 0.5), pos, norm, eyeDir, 2.0) / 4.0;
	spec += goldenSpec(vec3(4.0, 3.0, 2.5), pos, norm, eyeDir, 1.5) / 6.0;
	spec += goldenSpec(vec3(-33.0, 0.5, 30.0), pos, norm, eyeDir, 2.0) / 12.0;
	spec = clamp(spec, 0.0, 0.7);
	spec += mix(0.8, 1.0, specTexture)
		* goldenSpec(vec3(10.0, 0.5, 3.3), pos, norm, eyeDir, 2.0) / 8.0;
	spec += mix(0.8, 1.0, specTexture)
		* goldenSpec(vec3(-10.0, 0.5, 3.7), pos, norm, eyeDir, 2.0) / 8.0;
	spec += mix(0.8, 1.0, specTexture)
		* goldenSpec(vec3(0.5, 12.0, 1.5), pos, norm, eyeDir, 2.0) / 8.0;
	spec = clamp(spec, 0.0, 0.9);

	color = mix(vec4(vec3(0.0), 1.0), color, 0.8 + 0.3 * diffuse);
	vec4 specColor = vec4(mix(1.8, 2.0, specTexture) * gradientColor1, 1.0);
	color = mix(color, specColor, spec);

	float flecksSpec = goldenSpec(vec3(1.2, -0.2, 0.5), pos, norm, eyeDir, 2.0);
	color = mix(
		color,
		specColor,
		clamp(flecksSpec * abs(vNormal.z) * flecksNormal.z, 0.2, 0.3) - 0.2);

	return mix(color * alpha, vec4(1.0), white);
}

void main() {
	if (extra.z > 0.5) {
		fragColor = goldenStar();
		return;
	}

	float f_xOffset = params.x;
	float spec1 = params.y;
	float spec2 = params.z;
	float u_diffuse = params.w;
	float normalSpec = extra.x;
	float alpha = extra.y;
	vec3 gradientColor1 = grad1.rgb;
	vec3 gradientColor2 = grad2.rgb;
	vec3 normalSpecColor = vec3(1.0);

	vec3 cameraPosition = vec3(0.0, 0.0, 100.0);
	vec3 vLightPosition2 = vec3(-400.0, 400.0, 400.0);
	vec3 vLightPosition4 = vec3(0.0, 0.0, 100.0);
	vec3 vLightPositionNormal = vec3(100.0, -200.0, 400.0);

	vec3 vNormalW = normalize(vec3(world * vec4(vNormal, 0.0)));
	vec3 vTextureNormal = normalize(
		texture(u_NormalMap, (vUV + vec2(-f_xOffset, f_xOffset)) * 2.0).xyz
			* 2.0 - 1.0);
	vec3 finalNormal = normalize(vNormalW + vTextureNormal);

	vec3 color = texture(u_Texture, vUV).xyz;
	vec3 viewDirectionW = normalize(cameraPosition);

	vec3 angleW = normalize(viewDirectionW + vLightPosition2);
	float specComp2 = pow(max(0.0, dot(vNormalW, angleW)), 128.0) * spec1;

	angleW = normalize(viewDirectionW + vLightPosition4);
	float specComp3 = pow(max(0.0, dot(vNormalW, angleW)), 30.0) * spec2;

	float diffuse = max(dot(vNormalW, viewDirectionW), 1.0 - u_diffuse);

	float mixValue = distance(vUV, vec2(1.0, 0.0));
	vec3 gradientColorFinal = mix(gradientColor1, gradientColor2, mixValue);

	angleW = normalize(viewDirectionW + vLightPositionNormal);
	float normalSpecComp = pow(max(0.0, dot(finalNormal, angleW)), 128.0)
		* normalSpec;

	angleW = normalize(viewDirectionW + vLightPosition2);
	float normalSpecComp2 = pow(max(0.0, dot(finalNormal, angleW)), 128.0)
		* normalSpec;

	vec3 normalSpecFinal = normalSpecColor * (normalSpecComp + normalSpecComp2);
	vec3 specFinal = color * (specComp2 + specComp3);

	vec3 lit = gradientColorFinal + specFinal + normalSpecFinal;

	float outAlpha = clamp(diffuse, 0.0, 1.0) * alpha;
	fragColor = vec4(lit * outAlpha, outAlpha);
}
