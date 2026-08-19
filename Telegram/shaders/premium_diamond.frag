#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 objPos;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Shared {
	mat4 mvp;
	mat4 world;
	vec4 resolution;
	vec4 misc; // (time, night, alpha, _)
};

layout(std140, binding = 1) uniform Draw {
	vec4 drawParams; // (modelIndex, behind, _, _)
};

vec3 grad3(
		float x1, vec3 y1,
		float x2, vec3 y2,
		float x3, vec3 y3,
		float t) {
	if (t < x1) {
		return y1;
	} else if (t < x2) {
		return mix(y1, y2, (t - x1) / (x2 - x1));
	} else if (t < x3) {
		return mix(y2, y3, (t - x2) / (x3 - x2));
	}
	return y3;
}

void main() {
	float modelIndex = drawParams.x;
	bool behind = (drawParams.y > 0.5);
	float time = misc.x;
	bool night = (misc.y > 0.5);
	float alpha = misc.z;

	vec3 col = vec3(0.0);
	float a = 0.0;
	if (modelIndex < 0.5) {
		if (behind) {
			col = night
				? vec3(0.141, 0.341, 0.663)  // #2457A9
				: vec3(0.118, 0.412, 0.910); // #1E69E8
			a = 1.0;
		} else {
			// A single diagonal shine sweeping from lower-left to upper-right once
			// per cycle. The original periodic band repeated more often than the
			// quad is wide, so a stale stripe flashed at the far (upper-right) end
			// on the first frames of every cycle; a single pass avoids that.
			float phase = fract(time / 9.0);
			float visible = 1.1 / 16.2;
			if (phase > visible) {
				discard;
			}
			float sweep = phase / visible;
			vec2 pos = 3.0 * gl_FragCoord.xy / resolution.xy;
			float diag = pos.x - pos.y;
			float center = mix(-4.5, 4.5, sweep);
			float dist = diag - center;
			float body = (dist >= 0.0 && dist < 0.75) ? 0.4 : 0.0;
			float lead = (dist >= 1.0 && dist < 1.25) ? 0.4 : 0.0;
			a = max(body, lead);
			col = vec3(1.0);
		}
	} else if (modelIndex < 1.5) {
		vec3 p = objPos / 10.0 + 0.5;
		vec3 n = normalize((world * vec4(vNormal, 0.0)).xyz);
		if (behind) {
			float d = max(dot(n, normalize(vec3(-3.0, 20.0, 20.0) - p)), 0.0);
			col = mix(
				vec3(0.35, 0.59, 0.94),
				vec3(0.52, 0.70, 0.97),
				clamp(d, 0.0, 1.0));
			a = 1.0;
		} else {
			float s = 0.0;
			s += 0.5 * pow(max(dot(normalize(-p),
				reflect(-normalize(vec3(0.0, -100.0, 50.0) - p), n)), 0.0), 2.0);
			s += 0.4 * pow(max(dot(normalize(-p),
				reflect(-normalize(vec3(0.0) - p), n)), 0.0), 2.0);
			if (!night) {
				s *= 0.5;
			}
			col = vec3(1.0);
			a = s;
		}
	} else {
		vec3 p = objPos / 10.0 + 0.5;
		vec3 n = normalize((world * vec4(vNormal, 0.0)).xyz);
		float d = max(dot(n, normalize(vec3(-8.0, 12.0, 20.0) - p)), 0.0);
		float s = 0.75 * pow(max(dot(normalize(-p),
			reflect(-normalize(vec3(20.0, -150.0, 50.0) - p), n)), 0.0), 2.0);
		col = grad3(
			0.0, vec3(0.345, 0.659, 0.965),  // #58A8F6
			0.58, vec3(0.557, 0.831, 0.984), // #8ED4FB
			1.0, vec3(0.808, 0.910, 0.992),  // #CEE8FD
			d);
		col += vec3(0.773, 0.898, 0.988) * s; // #C5E5FC
		a = 1.0;
	}
	a *= alpha;
	fragColor = vec4(col * a, a);
}
