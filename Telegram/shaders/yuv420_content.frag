#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D y_texture;
layout(binding = 2) uniform sampler2D u_texture;
layout(binding = 3) uniform sampler2D v_texture;
layout(binding = 4) uniform sampler2D f_texture;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	// 1.0 when gl_FragCoord.y counts from the bottom (OpenGL).
	float fragCoordYUp;
	vec4 shadowTopRect;
	vec4 shadowBottomSkipOpacityFullFade;
	vec4 roundRect;
	float roundRadius;
	mat4 yuvToRgb;
	vec4 hdr;
};

float roundedCorner(vec2 fragCoord) {
	vec2 rectHalf = roundRect.zw / 2.0;
	vec2 rectCenter = roundRect.xy + rectHalf;
	vec2 fromRectCenter = abs(fragCoord - rectCenter);
	vec2 vectorRadius = vec2(roundRadius + 0.5);
	vec2 fromCenterWithRadius = fromRectCenter + vectorRadius;
	vec2 fromRoundingCenter = max(fromCenterWithRadius, rectHalf) - rectHalf;
	float rounded = length(fromRoundingCenter) - roundRadius;
	return 1.0 - smoothstep(0.0, 1.0, rounded);
}

const float kPqM1 = 0.1593017578125;
const float kPqM2 = 78.84375;
const float kPqC1 = 0.8359375;
const float kPqC2 = 18.8515625;
const float kPqC3 = 18.6875;
const float kHlgA = 0.17883277;
const float kHlgB = 0.28466892;
const float kHlgC = 0.55991073;
const float kHlgGamma = 1.2;
const float kHlgPeak = 0.1;
const float kWhite = 0.0203;
const float kOutputGamma = 2.2;

vec3 pqToLinear(vec3 value) {
	vec3 p = pow(clamp(value, 0.0, 1.0), vec3(1.0 / kPqM2));
	return pow(max(p - kPqC1, 0.0) / (kPqC2 - kPqC3 * p), vec3(1.0 / kPqM1));
}

float linearToPq(float value) {
	float p = pow(max(value, 0.0), kPqM1);
	return pow((kPqC1 + kPqC2 * p) / (1.0 + kPqC3 * p), kPqM2);
}

vec3 hlgToLinear(vec3 value) {
	value = clamp(value, 0.0, 1.0);
	vec3 low = value * value / 3.0;
	vec3 high = (exp((value - kHlgC) / kHlgA) + kHlgB) / 12.0;
	vec3 scene = mix(low, high, step(vec3(0.5), value));
	float luma = dot(scene, vec3(0.2627, 0.6780, 0.0593));
	return scene * (kHlgPeak * pow(luma, kHlgGamma - 1.0));
}

float toneMapLinear(float value) {
	float e1 = linearToPq(value) / hdr.z;
	if (e1 >= 1.0) {
		return kWhite;
	}
	float knee = 1.5 * hdr.w - 0.5;
	if (e1 <= knee) {
		return value;
	}
	float t = (e1 - knee) / (1.0 - knee);
	float t2 = t * t;
	float t3 = t2 * t;
	float e2 = (2.0 * t3 - 3.0 * t2 + 1.0) * knee
		+ (t3 - 2.0 * t2 + t) * (1.0 - knee)
		+ (-2.0 * t3 + 3.0 * t2) * hdr.w;
	return pqToLinear(vec3(e2 * hdr.z)).r;
}

vec3 toneMap(vec3 value) {
	vec3 rgbLinear = (hdr.x < 1.5) ? pqToLinear(value) : hlgToLinear(value);
	if (hdr.y > 0.5) {
		rgbLinear = mat3(
			1.660491, -0.124550, -0.018151,
			-0.587641, 1.132900, -0.100579,
			-0.072850, -0.008349, 1.118730) * rgbLinear;
	}
	rgbLinear = max(rgbLinear, 0.0);
	float top = max(max(rgbLinear.r, rgbLinear.g), rgbLinear.b);
	if (top > 0.0) {
		rgbLinear *= toneMapLinear(top) / top;
	}
	return pow(clamp(rgbLinear / kWhite, 0.0, 1.0), vec3(1.0 / kOutputGamma));
}

void main() {
	float fragY = (fragCoordYUp > 0.0)
		? gl_FragCoord.y
		: (viewport.y - gl_FragCoord.y);
	vec2 fragCoord = vec2(gl_FragCoord.x, fragY);
	vec3 yuv = vec3(
		texture(y_texture, v_texcoord).r,
		texture(u_texture, v_texcoord).r,
		texture(v_texture, v_texcoord).r);
	vec3 rgb = (yuvToRgb * vec4(yuv, 1.0)).rgb;
	if (hdr.x > 0.5) {
		rgb = toneMap(rgb);
	}
	vec4 result = vec4(rgb, 1.0);

	float topHeight = shadowTopRect.w;
	float bottomHeight = shadowBottomSkipOpacityFullFade.x;
	float bottomSkip = shadowBottomSkipOpacityFullFade.y;
	float opacity = shadowBottomSkipOpacityFullFade.z;
	float fullFade = shadowBottomSkipOpacityFullFade.w;
	float viewportHeight = shadowTopRect.y + topHeight;
	float fullHeight = topHeight + bottomHeight;
	float topY = min(
		(viewportHeight - fragCoord.y) / fullHeight,
		topHeight / fullHeight);
	float topX = (fragCoord.x - shadowTopRect.x) / shadowTopRect.z;
	vec4 fadeTop = texture(f_texture, vec2(topX, topY)) * opacity;
	float bottomY = max(bottomSkip + fullHeight - fragCoord.y, topHeight)
		/ fullHeight;
	vec4 fadeBottom = texture(f_texture, vec2(0.5, bottomY)) * opacity;
	float fade = min((1.0 - fadeTop.a) * (1.0 - fadeBottom.a), fullFade);
	result.rgb = result.rgb * fade;

	result *= roundedCorner(fragCoord);
	fragColor = result;
}
