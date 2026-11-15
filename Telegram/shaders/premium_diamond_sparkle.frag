#version 450

// Port of Telegram Android's sparkleFragment.glsl (InteractiveDiamond2),
// blue appearance only. The output stays premultiplied.

#define saturate(x) clamp(x, 0.0, 1.0)

layout(location = 0) in vec2 v_sourcePoint;
layout(location = 1) flat in float v_strength;
layout(location = 2) flat in uint v_layer;

layout(location = 0) out vec4 fragColor;

float radialOpacity(float radius, float first, float middle) {
	if (radius <= first) {
		return 1.0;
	} else if (radius <= middle) {
		return mix(1.0, 0.5, (radius - first) / (middle - first));
	}
	return 0.5 * saturate((1.0 - radius) / (1.0 - middle));
}

void main() {
	float alpha = 1.0;
	vec3 color = vec3(1.0);
	if (v_layer == 0u) {
		float r = length(v_sourcePoint - vec2(-0.5, 0.2)) / 108.5;
		alpha = radialOpacity(r, 0.312, 0.624) * 0.60;
	} else if (v_layer == 1u) {
		float r = length(v_sourcePoint - vec2(-0.5, 0.9)) / 113.1;
		alpha = radialOpacity(r, 0.297, 0.503) * 0.60;
		color = vec3(0.82, 1.0, 0.902);
	} else if (v_layer == 2u) {
		alpha = 0.96;
	} else if (v_layer == 3u) {
		float r = saturate(length(v_sourcePoint) / length(vec2(176.0, -170.0)));
		alpha = 1.0 - r;
		color = mix(vec3(0.694, 0.969, 1.0), vec3(0.663, 0.957, 1.0), r);
	} else if (v_layer == 5u) {
		alpha = radialOpacity(length(v_sourcePoint) / length(vec2(228.9, 3.9)), 0.237, 0.623);
	}
	alpha *= v_strength;
	fragColor = vec4(color * alpha, alpha);
}
