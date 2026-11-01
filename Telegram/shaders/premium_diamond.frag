#version 450

// Port of Telegram Android's diamondFragment.glsl (InteractiveDiamond2),
// blue appearance only, without the disabled internal refraction bounces.

#define saturate(x) clamp(x, 0.0, 1.0)

layout(location = 0) in vec3 v_localPosition;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec3 v_worldPosition;
layout(location = 3) in vec3 v_facetWeights;

layout(location = 0) out vec4 fragColor;

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

struct Raster {
	vec3 localPosition;
	vec3 normal;
	vec3 worldPosition;
	vec3 facetWeights; // table, crown, pavilion; interpolated across rounded edges
};

vec3 referenceCrown(vec2 p, vec4 gradient) {
	const float stops[7] = float[7](0.0, 0.162, 0.324, 0.501, 0.667, 0.833, 1.0);
	const vec3 colors[7] = vec3[7](vec3(0.145, 0.792, 1.0), vec3(0.373, 0.875, 1.0),
		vec3(0.6, 0.957, 1.0), vec3(0.3, 0.798, 1.0), vec3(0.0, 0.639, 1.0),
		vec3(0.0, 0.545, 1.0), vec3(0.0, 0.451, 1.0));
	vec2 start = gradient.xy, direction = gradient.zw - start;
	float t = saturate(dot(p - start, direction) / dot(direction, direction));
	for (uint i = 1u; i < 7u; ++i) {
		if (t <= stops[i]) {
			return mix(colors[i - 1u], colors[i], (t - stops[i - 1u]) / (stops[i] - stops[i - 1u]));
		}
	}
	return colors[6];
}

vec3 lateralDepth(vec3 color, vec3 normal, float y, vec3 facetWeights,
		float opticalLuminance) {
	float horizontalNormal = length(normal.xz);
	float side = smoothstep(0.22, 0.80, abs(normal.x) / max(horizontalNormal, 0.001));
	float shoulder = smoothstep(0.06, 0.57, y);
	float lower = smoothstep(0.12, 0.90, -y);
	float reflectedLight = smoothstep(0.28, 0.78, color.g);
	// A restrained blue-violet shadow keeps the sides distinct without
	// swallowing their cyan gradients and moving reflections.
	vec3 deepBlue = mix(vec3(0.017, 0.035, 0.90),
		vec3(0.006, 0.11, 1.0), reflectedLight);
	vec3 sideColor = mix(deepBlue, vec3(0.22, 0.86, 1.0), shoulder * 0.69);
	sideColor = mix(sideColor, vec3(0.018, 0.39, 1.0), lower * 0.52);
	float reflectionDetail = 0.105 + opticalLuminance * 0.14
		+ smoothstep(0.70, 0.95, color.g) * 0.26;
	sideColor = mix(sideColor, color, reflectionDetail);
	float weight = side * 0.96 * (1.0 - facetWeights.x);
	return mix(color, sideColor, weight);
}

vec3 facetBarycentric(vec2 p, vec2 a, vec2 b, vec2 c) {
	vec2 v = b - a, w = c - a, q = p - a;
	float determinant = v.x * w.y - v.y * w.x;
	float y = (q.x * w.y - q.y * w.x) / determinant;
	float z = (v.x * q.y - v.y * q.x) / determinant;
	return vec3(1.0 - y - z, y, z);
}

float facetCoverage(vec3 barycentric) {
	float edge = min(barycentric.x, min(barycentric.y, barycentric.z));
	float aa = max(fwidth(edge), 0.004);
	return smoothstep(-aa, aa, edge);
}

vec2 sourceFacetPoint(vec3 localPosition, vec2 outward) {
	float across = dot(localPosition.xz, vec2(outward.y, -outward.x));
	float depth = dot(localPosition.xz, outward);
	float c = u.facetProjection.x, s = u.facetProjection.y;
	float y = c * localPosition.y - s * depth;
	float z = s * localPosition.y + c * depth;
	float w = u.projection[2].w * z + u.projection[3].w;
	return vec2(257.6 + across / w * u.facetProjection.z,
		103.8 + (u.facetProjection.w - y / w) * u.facetProjection.z);
}

vec3 illustratedFacets(vec3 color, Raster inputData) {
	vec3 localNormal = normalize((u.inverseModel * vec4(normalize(inputData.normal), 0.0)).xyz);
	// Fixed material coordinates on each broad face. The same reference camera
	// maps the authored facets and the sparkle anchors onto the current cut.
	vec2 outward = abs(localNormal.x) > abs(localNormal.z)
		? vec2(sign(localNormal.x), 0.0) : vec2(0.0, sign(localNormal.z));
	vec3 tangent = vec3(outward.y, 0.0, -outward.x);
	vec2 p = sourceFacetPoint(inputData.localPosition, outward);
	// Fade the drawing before the diagonal facets, where the next face's
	// coordinate system takes over. No duplicated motifs along rounded edges.
	vec2 normalXZ = abs(localNormal.xz);
	float broad = 1.0 - smoothstep(0.20, 0.65, min(normalXZ.x, normalXZ.y) / max(max(normalXZ.x, normalXZ.y), 0.0001));
	vec3 worldTangent = (u.model * vec4(tangent, 0.0)).xyz;
	vec3 n = normalize(inputData.normal);
	vec3 light = normalize(vec3(0.6 * sin(u.lightSweep.y), 0.5, 1.0));
	float leftLight = pow(saturate(dot(normalize(n - worldTangent * 0.38), light)), 5.0);
	float rightLight = pow(saturate(dot(normalize(n + worldTangent * 0.38), light)), 5.0);
	if (inputData.facetWeights.y > 0.0) {
		// Both crown triangles meet at the same upper junction.
		const vec2 crownJunction = vec2(258.8, 128.9);
		// Authored lower corners now share the same projection as the mesh.
		const vec2 crownBaseLeft = vec2(108.8, 240.8);
		const vec2 crownBaseRight = vec2(406.8, 240.8);
		vec3 left = facetBarycentric(p, vec2(142.1, 106.3), crownJunction, crownBaseLeft);
		float leftStrength = facetCoverage(left) * (0.18 + 0.40 * leftLight) * saturate(1.0 - left.y);
		color = mix(color, vec3(0.79, 0.988, 1.0), leftStrength * inputData.facetWeights.y * broad);
		vec3 right = facetBarycentric(p, crownBaseRight, crownJunction, vec2(378.5, 108.2));
		// The source is light at the left junction and dark at the right edge.
		// Use horizontal facet coordinates so the gradient turns with the gem.
		float gradient = smoothstep(crownJunction.x, crownBaseRight.x, p.x);
		vec3 tint = mix(vec3(0.28, 0.83, 1.0), vec3(0.025, 0.43, 1.0), gradient);
		tint = mix(tint, vec3(0.57, 0.95, 1.0), rightLight * 0.24);
		color = mix(color, tint, facetCoverage(right) * 0.90 * inputData.facetWeights.y * broad);
	}
	if (inputData.facetWeights.z > 0.0) {
		// A translucent kite belongs to each broad pavilion face. Material-space
		// edges stay attached to the surface throughout a complete rotation.
		vec2 kite = vec2(dot(inputData.localPosition, tangent), inputData.localPosition.y);
		const float halfWidth = 0.245, top = -0.61, shoulder = -0.70, bottom = -1.075;
		const float upperHeight = top - shoulder, lowerHeight = shoulder - bottom;
		float upperEdge = ((top - kite.y) * halfWidth - abs(kite.x) * upperHeight)
			/ length(vec2(halfWidth, upperHeight));
		float lowerEdge = ((kite.y - bottom) * halfWidth - abs(kite.x) * lowerHeight)
			/ length(vec2(halfWidth, lowerHeight));
		float edge = min(upperEdge, lowerEdge);
		float aa = max(fwidth(edge), 0.0015);
		float coverage = smoothstep(-aa, aa, edge);
		float illumination = saturate(dot(n, normalize(vec3(-0.65, 0.5, 1.0))));
		vec3 tint = mix(vec3(0.48, 0.96, 1.0), vec3(0.70, 1.0, 1.0), illumination);
		float across = saturate(0.5 + kite.x / (2.0 * halfWidth));
		tint *= mix(vec3(1.0), vec3(0.88, 0.97, 1.0), across);
		color = mix(color, tint, coverage * 0.28 * inputData.facetWeights.z * broad);
	}
	return color;
}

vec3 studio(vec3 d, float phase) {
	float angle = 0.85 * sin(phase);
	d.xz = vec2(cos(angle) * d.x - sin(angle) * d.z, sin(angle) * d.x + cos(angle) * d.z);
	float up = smoothstep(-0.65, 0.85, d.y);
	vec3 c = mix(vec3(0.002, 0.075, 0.88), vec3(0.19, 0.78, 1.0), up);
	float key = pow(saturate(dot(d, normalize(vec3(-0.6, 0.8, 0.7)))), 10.0);
	float stripPosition = 0.12 + 0.48 * sin(phase);
	float strip = exp(-pow((d.x + d.y * 0.42 - stripPosition) * 9.0, 2.0));
	float side = pow(saturate(dot(d, normalize(vec3(0.8, 0.2, -0.5)))), 18.0);
	c = mix(c, vec3(0.68, 0.98, 1.0), key * 0.94);
	c = mix(c, vec3(0.80, 0.99, 1.0), strip * 0.64);
	c += side * vec3(0.1, 0.3, 0.38);
	return c;
}

vec2 facetEdgeRoll(vec2 p, vec2 a, vec2 b) {
	vec2 segment = b - a;
	float edgeLength = length(segment);
	vec2 along = segment / edgeLength;
	vec2 across = vec2(-along.y, along.x);
	float distance = dot(p - a, across);
	float progress = dot(p - a, along);
	float width = max(1.6, fwidth(distance));
	float q = distance / width;
	float ends = smoothstep(0.0, 5.0, progress) * (1.0 - smoothstep(edgeLength - 5.0, edgeLength, progress));
	return across * (q * exp(-q * q) * 0.22 * ends);
}

vec3 polishedNormal(Raster inputData, vec3 localNormal) {
	// Only the two surface crown diagonals get this tiny optical fillet.
	// The actual outline and the internal optical hull stay unchanged.
	if (inputData.facetWeights.y <= 0.0) {
		return normalize(inputData.normal);
	}
	vec2 outward = abs(localNormal.x) > abs(localNormal.z)
		? vec2(sign(localNormal.x), 0.0) : vec2(0.0, sign(localNormal.z));
	vec2 normalXZ = abs(localNormal.xz);
	float broad = 1.0 - smoothstep(0.20, 0.65, min(normalXZ.x, normalXZ.y) / max(max(normalXZ.x, normalXZ.y), 0.0001));
	vec2 p = sourceFacetPoint(inputData.localPosition, outward);
	vec2 roll = facetEdgeRoll(p, vec2(258.8, 128.9), vec2(108.8, 240.8))
		+ facetEdgeRoll(p, vec2(258.8, 128.9), vec2(406.8, 240.8));
	vec3 across = vec3(outward.y, 0.0, -outward.x);
	vec3 up = normalize(cross(localNormal, across));
	vec3 normal = normalize(localNormal - (across * roll.x - up * roll.y) * broad * inputData.facetWeights.y);
	return normalize((u.model * vec4(normal, 0.0)).xyz);
}

float studioPanel(vec3 reflected, vec3 direction, vec2 size) {
	float alignment = dot(reflected, direction);
	vec3 horizontal = normalize(cross(vec3(0.0, 1.0, 0.0), direction));
	vec3 vertical = cross(direction, horizontal);
	vec2 point = vec2(dot(reflected, horizontal), dot(reflected, vertical)) / max(alignment, 0.15);
	// Filter the narrow reflection at small sizes and grazing angles.
	vec2 dx = dFdx(point), dy = dFdy(point);
	vec2 variance = size * size + dx * dx + dy * dy;
	float energy = size.x * size.y / sqrt(variance.x * variance.y);
	return exp(-dot(point * point, 1.0 / variance)) * energy * smoothstep(0.15, 0.40, alignment);
}

vec3 surfaceFinish(vec3 color, vec3 normal, vec3 view) {
	vec3 reflected = reflect(-view, normal);
	float phase = u.lightSweep.y;
	float key = studioPanel(reflected, normalize(vec3(-0.36 + 0.14 * sin(phase), 0.90, 0.72)), vec2(0.16, 0.48));
	float rim = studioPanel(reflected, normalize(vec3(0.72, -0.52 + 0.10 * cos(phase), 0.18)), vec2(0.10, 0.60));
	float fresnel = pow(1.0 - saturate(dot(normal, view)), 4.0);
	float strength = key * (0.35 + 0.35 * fresnel) + rim * (0.16 + 0.30 * fresnel);
	return mix(color, vec3(0.87, 0.99, 1.0), strength);
}

float sourceBand(vec2 p, vec4 gradient) {
	vec2 direction = gradient.zw - gradient.xy;
	float t = dot(p - gradient.xy, direction) / dot(direction, direction);
	return saturate(1.0 - abs(t - 0.49) / 0.49);
}

float facetSweep(Raster inputData, vec3 localNormal) {
	vec3 tangent = normalize(vec3(localNormal.z + 0.00001, 0.0, -localNormal.x));
	float across = dot(inputData.localPosition, tangent) * 225.8;
	float side = smoothstep(0.20, 0.50, abs(normalize(inputData.normal).x));
	bool right = inputData.normal.x > 0.0;
	vec4 crown = mix(u.crownSweep, right ? u.rightCrownSweep : u.leftCrownSweep, side);
	vec4 pavilion = mix(u.pavilionSweep, right ? u.rightPavilionSweep : u.leftPavilionSweep, side);
	return sourceBand(vec2(across, (0.3065 - inputData.localPosition.y) * 239.0), crown) * inputData.facetWeights.y
		+ sourceBand(vec2(across, (-0.54 - inputData.localPosition.y) * 235.0), pavilion) * inputData.facetWeights.z;
}

float reflectionTriangle(vec3 origin, vec3 ray, vec3 a, vec3 b, vec3 c,
		out vec3 coordinates) {
	vec3 ab = b - a, ac = c - a, crossRay = cross(ray, ac);
	float determinant = dot(ab, crossRay);
	if (abs(determinant) < 0.00001) {
		coordinates = vec3(0.0);
		return 1e5;
	}
	vec3 offset = origin - a;
	float bu = dot(offset, crossRay) / determinant;
	vec3 q = cross(offset, ab);
	float bv = dot(ray, q) / determinant;
	float t = dot(ac, q) / determinant;
	coordinates = vec3(1.0 - bu - bv, bu, bv);
	return (min(min(coordinates.x, coordinates.y), coordinates.z) >= 0.0 && t > 0.001) ? t : 1e5;
}

vec3 pavilionReflections(vec3 color, vec3 origin, vec3 ray) {
	if (u.parameters.y <= 0.0) {
		return color;
	}
	const vec2 ring[8] = vec2[8](vec2(0.0, 1.0), vec2(0.70710678, 0.70710678),
		vec2(1.0, 0.0), vec2(0.70710678, -0.70710678), vec2(0.0, -1.0),
		vec2(-0.70710678, -0.70710678), vec2(-1.0, 0.0), vec2(-0.70710678, 0.70710678));
	// One convex fan of reflected facets, shared by every camera orientation.
	// Clip the whole volume so adjoining facets never acquire dark seams.
	const vec3 top = vec3(0.0, -0.28, 0.0), tip = vec3(0.0, -1.06, 0.0);
	float entry = 0.0, exit = 1e5;
	vec3 entryNormal = vec3(0.0, 1.0, 0.0);
	vec3 worldRay = normalize((u.model * vec4(ray, 0.0)).xyz);
	vec3 illumination = normalize(vec3(-0.55, 0.65, 1.0));
	vec3 sideColor = vec3(0.0);
	float sideWeight = 0.0;
	vec3 veilColor = vec3(0.0);
	float veilWeight = 0.0;
	for (uint i = 0u; i < 8u; ++i) {
		vec2 current = ring[i], next = ring[(i + 1u) % 8u];
		vec3 a = vec3(current.x * 0.28, -0.68, current.y * 0.28);
		vec3 b = vec3(next.x * 0.28, -0.68, next.y * 0.28);
		for (uint part = 0u; part < 2u; ++part) {
			vec3 peak = (part == 0u) ? top : tip;
			vec3 n = normalize(cross(a - peak, b - peak));
			if (dot(n, (a + b + peak) / 3.0 - vec3(0.0, -0.68, 0.0)) < 0.0) {
				n = -n;
			}
			float denominator = dot(n, ray);
			float side = dot(n, origin - peak);
			if (abs(denominator) < 0.00001) {
				if (side > 0.0) {
					exit = -1.0;
				}
			} else {
				float t = -side / denominator;
				if (denominator < 0.0 && t > entry) {
					entry = t;
					entryNormal = n;
				}
				if (denominator > 0.0) {
					exit = min(exit, t);
				}
			}
		}
		vec3 radial = vec3(current.x, 0.0, current.y);
		vec3 tangent = vec3(current.y, 0.0, -current.x);
		// Broad, low-contrast echoes of the crown stretch through the volume
		// towards the same lower junction. Adjacent planes meet without dark gaps.
		// Continue the same planes above the girdle so their top fade stays
		// hidden behind the crown, including the deeper, opposite reflections.
		const float upperY = 0.20;
		const float extension = (upperY + 0.12) / 0.65;
		const float upperRadius = 0.64 + (0.64 - 0.19) * extension;
		const float upperWidth = 0.265097 + (0.265097 - 0.078701) * extension;
		vec3 upperLeft = radial * upperRadius - tangent * upperWidth + vec3(0.0, upperY, 0.0);
		vec3 upperRight = radial * upperRadius + tangent * upperWidth + vec3(0.0, upperY, 0.0);
		vec3 lowerLeft = radial * 0.19 - tangent * 0.078701 + vec3(0.0, -0.77, 0.0);
		vec3 lowerRight = radial * 0.19 + tangent * 0.078701 + vec3(0.0, -0.77, 0.0);
		vec3 ribbonCoordinates;
		float ribbon = reflectionTriangle(origin, ray, upperLeft, upperRight, lowerLeft, ribbonCoordinates);
		if (ribbon > 100.0) {
			ribbon = reflectionTriangle(origin, ray, upperRight, lowerRight, lowerLeft, ribbonCoordinates);
		}
		if (ribbon < 100.0) {
			vec3 hit = origin + ray * ribbon;
			float depth = (-0.12 - hit.y) / 0.65;
			float across = dot(hit, tangent);
			float right = mix(0.265097, 0.078701, depth), left = -right;
			vec3 worldRadial = normalize((u.model * vec4(radial, 0.0)).xyz);
			float facing = saturate(-dot(worldRadial, worldRay));
			float fade = smoothstep(0.0, 0.065, upperY - hit.y) * (1.0 - smoothstep(0.70, 1.0, depth));
			float opacity = fade * (0.16 + 0.84 * facing);
			float light = saturate(0.5 + 0.5 * dot(worldRadial, illumination));
			// Broad reflected facets fill the middle with restrained diagonal
			// changes in tone, expressed in the fixed plane's own coordinates.
			float acrossPlane = saturate((across - left) / (right - left));
			float diagonal = smoothstep(-0.045, 0.045, acrossPlane - 0.35 - depth * 0.55);
			float middle = smoothstep(0.12, 0.24, depth) * (1.0 - smoothstep(0.50, 0.64, depth));
			vec3 tint = mix(vec3(0.015, 0.56, 1.0), vec3(0.33, 0.97, 1.0), light);
			tint = mix(tint, vec3(0.42, 0.97, 1.0), middle * (0.32 - 0.20 * diagonal));
			tint = mix(tint, vec3(0.015, 0.46, 1.0), diagonal * 0.24);
			veilColor += tint * opacity;
			veilWeight += opacity;
		}
		vec3 aSide = radial * 0.72 + tangent * 0.055 + vec3(0.0, -0.30, 0.0);
		vec3 bSide = radial * 0.49 - tangent * 0.105 + vec3(0.0, -0.60, 0.0);
		vec3 cSide = radial * 0.15 + tangent * 0.020 + vec3(0.0, -0.93, 0.0);
		vec3 coordinates;
		float t = reflectionTriangle(origin, ray, aSide, bSide, cSide, coordinates);
		if (t < 100.0) {
			vec3 worldRadial = normalize((u.model * vec4(radial, 0.0)).xyz);
			// Long glints read at the sides; a front-facing sector must not
			// become an opaque needle in the middle of the stone.
			float sideFacing = 1.0 - abs(dot(worldRadial, worldRay));
			float edge = min(coordinates.y, coordinates.z);
			float opacity = smoothstep(0.20, 0.75, sideFacing)
				* smoothstep(0.0, 0.085, edge) * smoothstep(0.0, 0.035, coordinates.x);
			float light = 0.5 + 0.5 * dot(worldRadial, illumination);
			vec3 tint = mix(vec3(0.16, 0.80, 1.0), vec3(0.85, 1.0, 1.0), light);
			sideColor += tint * opacity;
			sideWeight += opacity;
		}
	}
	float strength = saturate(u.parameters.y / 0.72);
	if (veilWeight > 0.0) {
		color = mix(color, veilColor / veilWeight, (1.0 - exp(-veilWeight * 0.46)) * strength);
	}
	if (sideWeight > 0.0) {
		color = mix(color, sideColor / sideWeight, (1.0 - exp(-sideWeight * 1.4)) * strength);
	}
	{
		// studioPanel uses screen derivatives. Evaluate it on both sides of the
		// hull boundary; divergent execution produces dotted edges on Adreno.
		vec3 hit = origin + ray * entry;
		vec3 worldNormal = normalize((u.model * vec4(entryNormal, 0.0)).xyz);
		float light = saturate(dot(worldNormal, normalize(vec3(-0.65, 0.15, 1.0))));
		float gleam = studioPanel(reflect(worldRay, worldNormal),
			normalize(vec3(-0.35 + 0.15 * sin(u.lightSweep.y), 0.60, 0.75)), vec2(0.25, 0.65));
		vec3 tint = mix(vec3(0.025, 0.54, 1.0), vec3(0.40, 0.92, 1.0), light);
		tint = mix(tint, vec3(0.72, 0.99, 1.0), gleam * 0.16);
		float thickness = smoothstep(0.0, 0.18, exit - entry);
		float fade = smoothstep(-1.07, -0.93, hit.y);
		color = mix(color, tint, 0.28 * thickness * fade * strength * (entry < exit ? 1.0 : 0.0));
	}
	return color;
}

vec4 diamondSurface(Raster inputData) {
	vec3 n = normalize(inputData.normal);
	float cameraDistance = -u.projection[3].w / u.projection[2].w;
	vec3 view = normalize(vec3(0.0, 0.0, cameraDistance) - inputData.worldPosition);
	vec3 localView = (u.inverseModel * vec4(-view, 0.0)).xyz;
	vec3 localNormal = normalize((u.inverseModel * vec4(n, 0.0)).xyz);
	vec3 transmitted = refract(localView, localNormal, 1.0 / 1.62);
	// Zero internal bounces on every device; retain the existing material mapping.
	vec3 optical = vec3(0.0);
	vec3 reflection = studio(reflect(-view, n), u.lightSweep.y);
	float fresnel = 0.08 + 0.46 * pow(1.0 - saturate(dot(n, view)), 4.0);

	float height = saturate((inputData.localPosition.y + 1.05) / 1.65);
	float key = saturate(dot(n, normalize(vec3(-0.65, 0.85, 1.0))));
	float left = saturate(0.52 - inputData.worldPosition.x * 0.43);
	vec3 blue = vec3(0.008, 0.22, 1.0);
	vec3 cyan = vec3(0.29, 0.87, 1.0);
	vec3 body = mix(blue, cyan, saturate(key * 0.60 + left * 0.38));
	float verticalBand = mix(0.5 + 0.5 * sin(height * 18.0 + n.x * 3.0), 1.0, inputData.facetWeights.z);
	body *= mix(vec3(0.24, 0.48, 0.96), vec3(1.0), verticalBand * 0.5 + 0.5);
	float opticalLuminance = smoothstep(0.25, 0.85, optical.g);
	// Give the pavilion a cyan body tone without changing reflection contrast
	// or the subdued lower motif. Crown and table keep their existing palette.
	vec3 opticalShadow = mix(vec3(0.005, 0.13, 1.0), vec3(0.015, 0.36, 1.0), inputData.facetWeights.z);
	vec3 opticalLight = mix(vec3(0.58, 0.97, 1.0), vec3(0.45, 0.99, 1.0), inputData.facetWeights.z);
	optical = mix(opticalShadow, opticalLight, opticalLuminance);
	float opticalWeight = u.parameters.y * mix(0.90, 0.08, inputData.facetWeights.y);
	vec3 color = mix(body, optical, opticalWeight);
	color = mix(color, reflection, fresnel);

	float crownGlow = smoothstep(0.10, 0.60, inputData.localPosition.y) * left;
	color = mix(color, vec3(0.70, 0.99, 1.0), crownGlow * 0.58);
	float crownLight = pow(saturate(dot(n, normalize(vec3(-0.38, 0.55, 1.0)))), 9.0);
	crownLight *= smoothstep(-0.03, 0.28, inputData.localPosition.y);
	color = mix(color, vec3(0.65, 0.98, 1.0), crownLight * 0.35);
	vec3 facetBase = color;
	if (inputData.facetWeights.y > 0.0) {
		float softbox = exp(-pow((inputData.worldPosition.x + 0.42) * 1.6, 2.0)
			- pow((inputData.worldPosition.y - 0.32) * 2.2, 2.0));
		color = mix(color, vec3(0.71, 0.99, 1.0), softbox * crownLight * 0.36);
		float shadow = pow(saturate(1.0 - key), 1.4);
		color = mix(color, vec3(0.015, 0.08, 1.0), shadow * 0.7);
		vec3 tangent = normalize(vec3(localNormal.z, 0.0, -localNormal.x));
		vec2 authoredPosition = vec2(dot(inputData.localPosition, tangent) * 225.8 + n.x * 65.0,
			(0.3065 - inputData.localPosition.y) * 244.0);
		color = mix(referenceCrown(authoredPosition, u.crownGradient), color, 0.28);
		color = mix(facetBase, color, inputData.facetWeights.y);
	}
	if (inputData.facetWeights.z > 0.0) {
		color = mix(color, mix(vec3(0.008, 0.32, 1.0), optical, 0.86), inputData.facetWeights.z * u.parameters.y);
	}
	float facetFlash = smoothstep(0.20, 0.78, opticalLuminance);
	float flashWeight = mix(0.24, 0.08, inputData.facetWeights.y);
	color *= mix(vec3(1.0), mix(vec3(0.76, 0.82, 0.99), vec3(1.0), facetFlash), flashWeight);
	color = mix(color, vec3(0.66, 0.98, 1.0), pow(facetFlash, 2.0) * 0.24 * flashWeight);
	color = lateralDepth(color, n, inputData.localPosition.y, inputData.facetWeights, opticalLuminance);
	color = mix(color, optical, inputData.facetWeights.z * u.parameters.y * 0.48);
	color = mix(color, body, inputData.facetWeights.z * pow(saturate(n.z), 2.0) * 0.32);
	if (inputData.facetWeights.z > 0.0) {
		vec3 detailRay = normalize(mix(localView, transmitted, 0.16));
		color = mix(color, pavilionReflections(color, inputData.localPosition, detailRay), inputData.facetWeights.z);
	}
	float tipGlow = pow(saturate((-inputData.localPosition.y - 0.55) / 0.50), 2.0);
	color = mix(color, vec3(0.57, 0.96, 1.0), tipGlow * 0.20);
	// A shallow, translucent image of the table, like Layer 36.0 in Lottie.
	// Its optical footprint is smaller than the rounded outer shoulder.
	vec3 tableRay = localView;
	tableRay.y *= 0.72;
	if (tableRay.y > 0.001) {
		float tableHeight = -u.planes[0].w / u.planes[0].y;
		float distance = (tableHeight - inputData.localPosition.y) / tableRay.y;
		vec2 hit = (inputData.localPosition + tableRay * distance).xz;
		const float extent = 0.632;
		const float corner = 0.285;
		vec2 q = abs(hit);
		float edge = max(max(q.x, q.y) - extent, (q.x + q.y - extent - corner) * 0.70710678);
		float aa = max(fwidth(edge), 0.001);
		float coverage = 1.0 - smoothstep(-aa, aa, edge);
		vec2 direction = tableRay.xz / max(length(tableRay.xz), 0.0001);
		float depth = saturate(0.5 + dot(hit, direction) / (2.0 * extent));
		// Source opacity: 80.0% group opacity times a 49...65% fill gradient.
		float opacity = mix(0.39, 0.52, depth);
		color = mix(color, vec3(0.765, 0.988, 1.0), coverage * opacity
			* saturate(u.parameters.y / 0.72) * inputData.facetWeights.y);
	}
	color = illustratedFacets(color, inputData);
	color = mix(color, vec3(0.63, 0.96, 1.0), 0.58 * inputData.facetWeights.x);
	float sweep = facetSweep(inputData, localNormal);
	float sweepCore = smoothstep(0.35, 1.0, sweep);
	color = mix(color, vec3(0.592, 0.953, 1.0), sweep * 0.20 + sweepCore * sweepCore * 0.62);
	color = surfaceFinish(color, polishedNormal(inputData, localNormal), view);
	return vec4(saturate(color * u.parameters.z), 1.0);
}

void main() {
	Raster inputData;
	inputData.localPosition = v_localPosition;
	inputData.normal = v_normal;
	inputData.worldPosition = v_worldPosition;
	inputData.facetWeights = v_facetWeights;
	fragColor = diamondSurface(inputData) * u.composition.x;
}
