/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_viewport_opengl.h"

#include "calls/group/calls_group_viewport_tile.h"
#include "webrtc/webrtc_video_track.h"
#include "media/view/media_view_pip.h"
#include "calls/group/calls_group_members_row.h"
#include "lang/lang_keys.h"
#include "ui/gl/gl_shader.h"
#include "data/data_peer.h"
#include "styles/style_calls.h"

#include <QtGui/QOpenGLShader>

namespace Calls::Group {
namespace {

using namespace Ui::GL;

constexpr auto kScaleForBlurTextureIndex = 3;
constexpr auto kFirstBlurPassTextureIndex = 4;
constexpr auto kNoiseTextureSize = 256;

// The more the scale - more blurred the image.
constexpr auto kBlurTextureSizeFactor = 4.;
constexpr auto kBlurOpacity = 0.65;
constexpr auto kDitherNoiseAmount = 0.002;
constexpr auto kMinCameraVisiblePart = 0.75;

constexpr auto kQuads = 9;
constexpr auto kQuadVertices = kQuads * 4;
constexpr auto kQuadValues = kQuadVertices * 4;
constexpr auto kValues = kQuadValues + 8; // Blur texture coordinates.

[[nodiscard]] ShaderPart FragmentBlurTexture(
		bool vertical,
		char prefix = 'v') {
	const auto offsets = (vertical ? QString("0, 1") : QString("1, 0"));
	const auto name = prefix + QString("_texcoord");
	return {
		.header = R"(
varying vec2 )" + name + R"(;
uniform sampler2D b_texture;
uniform float texelOffset;
const vec3 satLuminanceWeighting = vec3(0.2126, 0.7152, 0.0722);
const vec2 offsets = vec2()" + offsets + R"();
const int radius = 15;
const int diameter = 2 * radius + 1;
)",
		.body = R"(
	vec4 accumulated = vec4(0.);
	for (int i = 0; i != diameter; i++) {
		float stepOffset = float(i - radius) * texelOffset;
		vec2 offset = vec2(stepOffset) * offsets;
		vec4 sampled = vec4(texture2D(b_texture, )" + name + R"( + offset));
		float fradius = float(radius);
		float boxWeight = fradius + 1.0 - abs(float(i) - fradius);
		accumulated += sampled * boxWeight;
	}
	vec3 blurred = accumulated.rgb / accumulated.a;
	float satLuminance = dot(blurred, satLuminanceWeighting);
	vec3 mixinColor = vec3(satLuminance);
	result = vec4(clamp(mix(mixinColor, blurred, 1.1), 0.0, 1.0), 1.0);
)",
	};
}

[[nodiscard]] ShaderPart FragmentGenerateNoise() {
	const auto size = QString::number(kNoiseTextureSize);
	return {
		.header = R"(
const float permTexUnit = 1.0 / )" + size + R"(.0;
const float permTexUnitHalf = 0.5 / )" + size + R"(.0;
const float grainsize = 1.3;
const float noiseCoordRotation = 1.425;
const vec2 dimensions = vec2()" + size + ", " + size + R"();

vec4 rnm(vec2 tc) {
	float noise = sin(dot(tc, vec2(12.9898, 78.233))) * 43758.5453;
	return vec4(
		fract(noise),
		fract(noise * 1.2154),
		fract(noise * 1.3453),
		fract(noise * 1.3647)
	) * 2.0 - 1.0;
}

float fade(float t) {
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float pnoise3D(vec3 p) {
	vec3 pi = permTexUnit * floor(p) + permTexUnitHalf;
	vec3 pf = fract(p);
	float perm = rnm(pi.xy).a;
	float n000 = dot(rnm(vec2(perm, pi.z)).rgb * 4.0 - 1.0, pf);
	float n001 = dot(
		rnm(vec2(perm, pi.z + permTexUnit)).rgb * 4.0 - 1.0,
		pf - vec3(0.0, 0.0, 1.0));
	perm = rnm(pi.xy + vec2(0.0, permTexUnit)).a;
	float n010 = dot(
		rnm(vec2(perm, pi.z)).rgb * 4.0 - 1.0,
		pf - vec3(0.0, 1.0, 0.0));
	float n011 = dot(
		rnm(vec2(perm, pi.z + permTexUnit)).rgb * 4.0 - 1.0,
		pf - vec3(0.0, 1.0, 1.0));
	perm = rnm(pi.xy + vec2(permTexUnit, 0.0)).a;
	float n100 = dot(
		rnm(vec2(perm, pi.z)).rgb * 4.0 - 1.0,
		pf - vec3(1.0, 0.0, 0.0));
	float n101 = dot(
		rnm(vec2(perm, pi.z + permTexUnit)).rgb * 4.0 - 1.0,
		pf - vec3(1.0, 0.0, 1.0));
	perm = rnm(pi.xy + vec2(permTexUnit, permTexUnit)).a;
	float n110 = dot(
		rnm(vec2(perm, pi.z)).rgb * 4.0 - 1.0,
		pf - vec3(1.0, 1.0, 0.0));
	float n111 = dot(
		rnm(vec2(perm, pi.z + permTexUnit)).rgb * 4.0 - 1.0,
		pf - vec3(1.0, 1.0, 1.0));
	vec4 n_x = mix(
		vec4(n000, n001, n010, n011),
		vec4(n100, n101, n110, n111),
		fade(pf.x));
	vec2 n_xy = mix(n_x.xy, n_x.zw, fade(pf.y));
	return mix(n_xy.x, n_xy.y, fade(pf.z));
}

vec2 rotateTexCoords(in lowp vec2 tc, in lowp float angle) {
	float cosa = cos(angle);
	float sina = sin(angle);
	return vec2(
		((tc.x * 2.0 - 1.0) * cosa - (tc.y * 2.0 - 1.0) * sina) * 0.5 + 0.5,
		((tc.y * 2.0 - 1.0) * cosa + (tc.x * 2.0 - 1.0) * sina) * 0.5 + 0.5);
}
)",
		.body = R"(
	vec2 rotatedCoords = rotateTexCoords(
		gl_FragCoord.xy / dimensions.xy,
		noiseCoordRotation);
	float intensity = pnoise3D(vec3(
		rotatedCoords.x * dimensions.x / grainsize,
		rotatedCoords.y * dimensions.y / grainsize,
		0.0));

	// Looks like intensity is almost always in [-2, 2] range.
	float clamped = clamp((intensity + 2.) * 0.25, 0., 1.);
	result = vec4(clamped, 0., 0., 1.);
)",
	};
}

[[nodiscard]] ShaderPart FragmentDitherNoise() {
	const auto size = QString::number(kNoiseTextureSize);
	return {
		.header = R"(
uniform sampler2D n_texture;
)",
		.body = R"(
	vec2 noiseTextureCoord = gl_FragCoord.xy / )" + size + R"(.;
	float noiseClamped = texture2D(n_texture, noiseTextureCoord).r;
	float noiseIntensity = (noiseClamped * 4.) - 2.;

	vec3 lumcoeff = vec3(0.299, 0.587, 0.114);
	float luminance = dot(result.rgb, lumcoeff);
	float lum = smoothstep(0.2, 0.0, luminance) + luminance;
	vec3 noiseColor = mix(vec3(noiseIntensity), vec3(0.0), pow(lum, 4.0));

	result.rgb = result.rgb + noiseColor * noiseGrain;
)",
	};
}

// Depends on FragmentSampleTexture().
[[nodiscard]] ShaderPart FragmentFrameColor() {
	const auto round = FragmentRoundCorners();
	const auto blur = FragmentBlurTexture(true, 'b');
	const auto noise = FragmentDitherNoise();
	return {
		.header = R"(
uniform vec4 frameBg;
uniform vec3 shadow; // fullHeight, shown, maxOpacity
uniform float paused; // 0. <-> 1.

)" + blur.header + round.header + noise.header + R"(

const float backgroundOpacity = )" + QString::number(kBlurOpacity) + R"(;
const float noiseGrain = )" + QString::number(kDitherNoiseAmount) + R"(;

float insideTexture() {
	vec2 textureHalf = vec2(0.5, 0.5);
	vec2 fromTextureCenter = abs(v_texcoord - textureHalf);
	vec2 fromTextureEdge = max(fromTextureCenter, textureHalf) - textureHalf;
	float outsideCheck = dot(fromTextureEdge, fromTextureEdge);
	return step(outsideCheck, 0.);
}

vec4 background() {
	vec4 result;

)" + blur.body + noise.body + R"(

	return result;
}
)",
		.body = R"(
	float inside = insideTexture() * (1. - paused);
	result = result * inside
		+ (1. - inside) * (backgroundOpacity * background()
			+ (1. - backgroundOpacity) * frameBg);

	float shadowCoord = gl_FragCoord.y - roundRect.y;
	float shadowValue = max(1. - (shadowCoord / shadow.x), 0.);
	float shadowShown = max(shadowValue * shadow.y, paused) * shadow.z;
	result = vec4(result.rgb * (1. - shadowShown), result.a);
)" + round.body,
	};
}

[[nodiscard]] bool UseExpandForCamera(QSize original, QSize viewport) {
	const auto big = original.scaled(
		viewport,
		Qt::KeepAspectRatioByExpanding);

	// If we cut out no more than 0.25 of the original, let's use expanding.
	return (big.width() * kMinCameraVisiblePart <= viewport.width())
		&& (big.height() * kMinCameraVisiblePart <= viewport.height());
}

[[nodiscard]] QSize NonEmpty(QSize size) {
	return QSize(std::max(size.width(), 1), std::max(size.height(), 1));
}

[[nodiscard]] QSize CountBlurredSize(
		QSize unscaled,
		QSize outer,
		float factor) {
	factor *= kBlurTextureSizeFactor;
	const auto area = outer / int(std::round(factor * cScale() / 100));
	const auto scaled = unscaled.scaled(area, Qt::KeepAspectRatio);
	return (scaled.width() > unscaled.width()
		|| scaled.height() > unscaled.height())
		? unscaled
		: NonEmpty(scaled);
}

[[nodiscard]] QSize InterpolateScaledSize(
		QSize unscaled,
		QSize size,
		float64 ratio) {
	if (ratio == 0.) {
		return NonEmpty(unscaled.scaled(
			size,
			Qt::KeepAspectRatio));
	} else if (ratio == 1.) {
		return NonEmpty(unscaled.scaled(
			size,
			Qt::KeepAspectRatioByExpanding));
	}
	const auto notExpanded = NonEmpty(unscaled.scaled(
		size,
		Qt::KeepAspectRatio));
	const auto expanded = NonEmpty(unscaled.scaled(
		size,
		Qt::KeepAspectRatioByExpanding));
	return QSize(
		anim::interpolate(notExpanded.width(), expanded.width(), ratio),
		anim::interpolate(notExpanded.height(), expanded.height(), ratio));
}

[[nodiscard]] std::array<std::array<GLfloat, 2>, 4> CountTexCoords(
		QSize unscaled,
		QSize size,
		float64 expandRatio,
		bool swap = false) {
	const auto scaled = InterpolateScaledSize(unscaled, size, expandRatio);
	const auto left = (size.width() - scaled.width()) / 2;
	const auto top = (size.height() - scaled.height()) / 2;
	auto dleft = float(left) / scaled.width();
	auto dright = float(size.width() - left) / scaled.width();
	auto dtop = float(top) / scaled.height();
	auto dbottom = float(size.height() - top) / scaled.height();
	if (swap) {
		std::swap(dleft, dtop);
		std::swap(dright, dbottom);
	}
	return { {
		{ { -dleft, 1.f + dtop } },
		{ { dright, 1.f + dtop } },
		{ { dright, 1.f - dbottom } },
		{ { -dleft, 1.f - dbottom } },
	} };
}

} // namespace

Viewport::RendererGL::RendererGL(not_null<Viewport*> owner)
: _owner(owner)
, _pinIcon(st::groupCallVideoTile.pin)
, _muteIcon(st::groupCallVideoCrossLine)
, _pinBackground(
	(st::groupCallVideoTile.pinPadding.top()
		+ st::groupCallVideoTile.pin.icon.height()
		+ st::groupCallVideoTile.pinPadding.bottom()) / 2,
	st::radialBg) {

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_buttons.invalidate();
	}, _lifetime);
}

void Viewport::RendererGL::init(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_frameBuffer.emplace();
	_frameBuffer->setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_frameBuffer->create();
	_frameBuffer->bind();
	_frameBuffer->allocate(kValues * sizeof(GLfloat));
	_downscaleProgram.yuv420.emplace();
	const auto downscaleVertexSource = VertexShader({
		VertexPassTextureCoord(),
	});
	_downscaleVertexShader = LinkProgram(
		&*_downscaleProgram.yuv420,
		VertexShader({
			VertexPassTextureCoord(),
		}),
		FragmentShader({
			FragmentSampleYUV420Texture(),
		})).vertex;
	if (!_downscaleProgram.yuv420->isLinked()) {
		//...
	}
	_blurProgram.emplace();
	LinkProgram(
		&*_blurProgram,
		_downscaleVertexShader,
		FragmentShader({
			FragmentBlurTexture(false),
		}));
	_frameProgram.yuv420.emplace();
	_frameVertexShader = LinkProgram(
		&*_frameProgram.yuv420,
		VertexShader({
			VertexViewportTransform(),
			VertexPassTextureCoord(),
			VertexPassTextureCoord('b'),
		}),
		FragmentShader({
			FragmentSampleYUV420Texture(),
			FragmentFrameColor(),
		})).vertex;

	_imageProgram.emplace();
	LinkProgram(
		&*_imageProgram,
		VertexShader({
			VertexViewportTransform(),
			VertexPassTextureCoord(),
		}),
		FragmentShader({
			FragmentSampleARGB32Texture(),
			FragmentGlobalOpacity(),
		}));

	validateNoiseTexture(f, 0);
}

void Viewport::RendererGL::ensureARGB32Program() {
	Expects(_downscaleVertexShader != nullptr);
	Expects(_frameVertexShader != nullptr);

	_downscaleProgram.argb32.emplace();
	LinkProgram(
		&*_downscaleProgram.argb32,
		_downscaleVertexShader,
		FragmentShader({
			FragmentSampleARGB32Texture(),
		}));

	_frameProgram.argb32.emplace();
	LinkProgram(
		&*_frameProgram.argb32,
		_frameVertexShader,
		FragmentShader({
			FragmentSampleARGB32Texture(),
			FragmentFrameColor(),
		}));
}

void Viewport::RendererGL::deinit(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_frameBuffer = std::nullopt;
	_frameVertexShader = nullptr;
	_imageProgram = std::nullopt;
	_downscaleProgram.argb32 = std::nullopt;
	_downscaleProgram.yuv420 = std::nullopt;
	_blurProgram = std::nullopt;
	_frameProgram.argb32 = std::nullopt;
	_frameProgram.yuv420 = std::nullopt;
	_noiseTexture.destroy(f);
	_noiseFramebuffer.destroy(f);
	for (auto &data : _tileData) {
		data.textures.destroy(f);
	}
	_tileData.clear();
	_tileDataIndices.clear();
	_buttons.destroy(f);
}

void Viewport::RendererGL::setDefaultViewport(QOpenGLFunctions &f) {
	const auto size = _viewport * _factor;
	f.glViewport(0, 0, size.width(), size.height());
}

void Viewport::RendererGL::paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	const auto factor = widget->devicePixelRatio();
	if (_factor != factor) {
		_factor = factor;
		_buttons.invalidate();
	}
	_viewport = widget->size();

	const auto defaultFramebufferObject = widget->defaultFramebufferObject();

	validateDatas();
	auto index = 0;
	for (const auto &tile : _owner->_tiles) {
		if (!tile->visible()) {
			index++;
			continue;
		}
		paintTile(
			f,
			defaultFramebufferObject,
			tile.get(),
			_tileData[_tileDataIndices[index++]]);
	}
}

std::optional<QColor> Viewport::RendererGL::clearColor() {
	return st::groupCallBg->c;
}

void Viewport::RendererGL::validateUserpicFrame(
		not_null<VideoTile*> tile,
		TileData &tileData) {
	if (!_userpicFrame) {
		tileData.userpicFrame = QImage();
		return;
	} else if (!tileData.userpicFrame.isNull()) {
		return;
	}
	tileData.userpicFrame = QImage(
		tile->trackOrUserpicSize(),
		QImage::Format_ARGB32_Premultiplied);
	tileData.userpicFrame.fill(Qt::black);
	{
		auto p = Painter(&tileData.userpicFrame);
		tile->row()->peer()->paintUserpicSquare(
			p,
			tile->row()->ensureUserpicView(),
			0,
			0,
			tileData.userpicFrame.width());
	}
}

void Viewport::RendererGL::paintTile(
		QOpenGLFunctions &f,
		GLuint defaultFramebufferObject,
		not_null<VideoTile*> tile,
		TileData &tileData) {
	const auto track = tile->track();
	const auto markGuard = gsl::finally([&] {
		tile->track()->markFrameShown();
	});
	const auto data = track->frameWithInfo(false);
	_userpicFrame = (data.format == Webrtc::FrameFormat::None);
	validateUserpicFrame(tile, tileData);
	const auto frameSize = _userpicFrame
		? tileData.userpicFrame.size()
		: data.yuv420->size;
	const auto frameRotation = _userpicFrame
		? 0
		: data.rotation;
	Assert(!frameSize.isEmpty());

	_rgbaFrame = (data.format == Webrtc::FrameFormat::ARGB32)
		|| _userpicFrame;
	const auto geometry = tile->geometry();
	const auto x = geometry.x();
	const auto y = geometry.y();
	const auto width = geometry.width();
	const auto height = geometry.height();
	const auto &st = st::groupCallVideoTile;
	const auto shown = _owner->_controlsShownRatio;
	const auto fullNameShift = st.namePosition.y() + st::normalFont->height;
	const auto nameShift = anim::interpolate(fullNameShift, 0, shown);
	const auto row = tile->row();

	validateOutlineAnimation(tile, tileData);
	validatePausedAnimation(tile, tileData);
	const auto outline = tileData.outlined.value(tileData.outline ? 1. : 0.);
	const auto paused = tileData.paused.value(tileData.pause ? 1. : 0.);

	ensureButtonsImage();

	// Frame.
	const auto unscaled = Media::View::FlipSizeByRotation(
		frameSize,
		frameRotation);
	const auto tileSize = geometry.size();
	const auto swap = (((frameRotation / 90) % 2) == 1);
	const auto expand = isExpanded(tile, unscaled, tileSize);
	const auto animation = tile->animation();
	const auto expandRatio = (animation.ratio >= 0.)
		? countExpandRatio(tile, unscaled, animation)
		: expand
		? 1.
		: 0.;
	auto texCoords = CountTexCoords(unscaled, tileSize, expandRatio, swap);
	auto blurTexCoords = (expandRatio == 1. && !swap)
		? texCoords
		: CountTexCoords(unscaled, tileSize, 1.);
	const auto rect = transformRect(geometry);
	auto toBlurTexCoords = std::array<std::array<GLfloat, 2>, 4> { {
		{ { 0.f, 1.f } },
		{ { 1.f, 1.f } },
		{ { 1.f, 0.f } },
		{ { 0.f, 0.f } },
	} };
	if (const auto shift = (frameRotation / 90); shift > 0) {
		std::rotate(
			toBlurTexCoords.begin(),
			toBlurTexCoords.begin() + shift,
			toBlurTexCoords.end());
		std::rotate(
			texCoords.begin(),
			texCoords.begin() + shift,
			texCoords.end());
	}

	const auto nameTop = y + (height
		- st.namePosition.y()
		- st::semiboldFont->height);

	// Paused icon and text.
	const auto middle = (st::groupCallVideoPlaceholderHeight
		- st::groupCallPaused.height()) / 2;
	const auto pausedSpace = (nameTop - y)
		- st::groupCallPaused.height()
		- st::semiboldFont->height;
	const auto pauseIconSkip = middle - st::groupCallVideoPlaceholderIconTop;
	const auto pauseTextSkip = st::groupCallVideoPlaceholderTextTop
		- st::groupCallVideoPlaceholderIconTop;
	const auto pauseIconTop = !_owner->wide()
		? (y + (height - st::groupCallPaused.height()) / 2)
		: (pausedSpace < 3 * st::semiboldFont->height)
		? (pausedSpace / 3)
		: std::min(
			y + (height / 2) - pauseIconSkip,
			(nameTop
				- st::semiboldFont->height * 3
				- st::groupCallPaused.height()));
	const auto pauseTextTop = (pausedSpace < 3 * st::semiboldFont->height)
		? (nameTop - (pausedSpace / 3) - st::semiboldFont->height)
		: std::min(
			pauseIconTop + pauseTextSkip,
			nameTop - st::semiboldFont->height * 2);

	const auto pauseIcon = _buttons.texturedRect(
		QRect(
			x + (width - st::groupCallPaused.width()) / 2,
			pauseIconTop,
			st::groupCallPaused.width(),
			st::groupCallPaused.height()),
		_paused);
	const auto pauseRect = transformRect(pauseIcon.geometry);

	const auto pausedPosition = QPoint(
		x + (width - (_pausedTextRect.width() / cIntRetinaFactor())) / 2,
		pauseTextTop);
	const auto pausedText = _names.texturedRect(
		QRect(pausedPosition, _pausedTextRect.size() / cIntRetinaFactor()),
		_pausedTextRect);
	const auto pausedRect = transformRect(pausedText.geometry);

	// Pin.
	const auto pin = _buttons.texturedRect(
		tile->pinInner().translated(x, y),
		tile->pinned() ? _pinOn : _pinOff,
		geometry);
	const auto pinRect = transformRect(pin.geometry);

	// Back.
	const auto back = _buttons.texturedRect(
		tile->backInner().translated(x, y),
		_back,
		geometry);
	const auto backRect = transformRect(back.geometry);

	// Mute.
	const auto &icon = st::groupCallVideoCrossLine.icon;
	const auto iconLeft = x + width - st.iconPosition.x() - icon.width();
	const auto iconTop = y + (height
		- st.iconPosition.y()
		- icon.height()
		+ nameShift);
	const auto mute = _buttons.texturedRect(
		QRect(iconLeft, iconTop, icon.width(), icon.height()),
		(row->state() == MembersRow::State::Active
			? _muteOff
			: _muteOn),
		geometry);
	const auto muteRect = transformRect(mute.geometry);

	// Name.
	const auto namePosition = QPoint(
		x + st.namePosition.x(),
		nameTop + nameShift);
	const auto name = _names.texturedRect(
		QRect(namePosition, tileData.nameRect.size() / cIntRetinaFactor()),
		tileData.nameRect,
		geometry);
	const auto nameRect = transformRect(name.geometry);

	const GLfloat coords[] = {
		// YUV -> RGB-for-blur quad.
		-1.f, 1.f,
		toBlurTexCoords[0][0], toBlurTexCoords[0][1],

		1.f, 1.f,
		toBlurTexCoords[1][0], toBlurTexCoords[1][1],

		1.f, -1.f,
		toBlurTexCoords[2][0], toBlurTexCoords[2][1],

		-1.f, -1.f,
		toBlurTexCoords[3][0], toBlurTexCoords[3][1],

		// First RGB -> RGB blur pass.
		-1.f, 1.f,
		0.f, 1.f,

		1.f, 1.f,
		1.f, 1.f,

		1.f, -1.f,
		1.f, 0.f,

		-1.f, -1.f,
		0.f, 0.f,

		// Second blur pass + paint final frame.
		rect.left(), rect.top(),
		texCoords[0][0], texCoords[0][1],

		rect.right(), rect.top(),
		texCoords[1][0], texCoords[1][1],

		rect.right(), rect.bottom(),
		texCoords[2][0], texCoords[2][1],

		rect.left(), rect.bottom(),
		texCoords[3][0], texCoords[3][1],

		// Additional blurred background texture coordinates.
		blurTexCoords[0][0], blurTexCoords[0][1],
		blurTexCoords[1][0], blurTexCoords[1][1],
		blurTexCoords[2][0], blurTexCoords[2][1],
		blurTexCoords[3][0], blurTexCoords[3][1],

		// Pin button.
		pinRect.left(), pinRect.top(),
		pin.texture.left(), pin.texture.bottom(),

		pinRect.right(), pinRect.top(),
		pin.texture.right(), pin.texture.bottom(),

		pinRect.right(), pinRect.bottom(),
		pin.texture.right(), pin.texture.top(),

		pinRect.left(), pinRect.bottom(),
		pin.texture.left(), pin.texture.top(),

		// Back button.
		backRect.left(), backRect.top(),
		back.texture.left(), back.texture.bottom(),

		backRect.right(), backRect.top(),
		back.texture.right(), back.texture.bottom(),

		backRect.right(), backRect.bottom(),
		back.texture.right(), back.texture.top(),

		backRect.left(), backRect.bottom(),
		back.texture.left(), back.texture.top(),

		// Mute icon.
		muteRect.left(), muteRect.top(),
		mute.texture.left(), mute.texture.bottom(),

		muteRect.right(), muteRect.top(),
		mute.texture.right(), mute.texture.bottom(),

		muteRect.right(), muteRect.bottom(),
		mute.texture.right(), mute.texture.top(),

		muteRect.left(), muteRect.bottom(),
		mute.texture.left(), mute.texture.top(),

		// Name.
		nameRect.left(), nameRect.top(),
		name.texture.left(), name.texture.bottom(),

		nameRect.right(), nameRect.top(),
		name.texture.right(), name.texture.bottom(),

		nameRect.right(), nameRect.bottom(),
		name.texture.right(), name.texture.top(),

		nameRect.left(), nameRect.bottom(),
		name.texture.left(), name.texture.top(),

		// Paused icon.
		pauseRect.left(), pauseRect.top(),
		pauseIcon.texture.left(), pauseIcon.texture.bottom(),

		pauseRect.right(), pauseRect.top(),
		pauseIcon.texture.right(), pauseIcon.texture.bottom(),

		pauseRect.right(), pauseRect.bottom(),
		pauseIcon.texture.right(), pauseIcon.texture.top(),

		pauseRect.left(), pauseRect.bottom(),
		pauseIcon.texture.left(), pauseIcon.texture.top(),

		// Paused text.
		pausedRect.left(), pausedRect.top(),
		pausedText.texture.left(), pausedText.texture.bottom(),

		pausedRect.right(), pausedRect.top(),
		pausedText.texture.right(), pausedText.texture.bottom(),

		pausedRect.right(), pausedRect.bottom(),
		pausedText.texture.right(), pausedText.texture.top(),

		pausedRect.left(), pausedRect.bottom(),
		pausedText.texture.left(), pausedText.texture.top(),
	};

	_frameBuffer->bind();
	_frameBuffer->write(0, coords, sizeof(coords));

	const auto blurSize = CountBlurredSize(
		unscaled,
		geometry.size(),
		_factor);
	prepareObjects(f, tileData, blurSize);
	f.glViewport(0, 0, blurSize.width(), blurSize.height());

	bindFrame(f, data, tileData, _downscaleProgram);

	drawDownscalePass(f, tileData);
	drawFirstBlurPass(f, tileData, blurSize);

	f.glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject);
	setDefaultViewport(f);

	bindFrame(f, data, tileData, _frameProgram);

	const auto program = _rgbaFrame
		? &*_frameProgram.argb32
		: &*_frameProgram.yuv420;
	const auto uniformViewport = QSizeF(_viewport * _factor);

	program->setUniformValue("viewport", uniformViewport);
	program->setUniformValue("frameBg", st::groupCallBg->c);
	program->setUniformValue("radiusOutline", QVector2D(
		GLfloat(st::roundRadiusLarge * _factor),
		(outline > 0) ? (st::groupCallOutline * _factor) : 0.f));
	program->setUniformValue("roundRect", Uniform(rect));
	program->setUniformValue("roundBg", st::groupCallBg->c);
	program->setUniformValue("outlineFg", QVector4D(
		st::groupCallMemberActiveIcon->c.redF(),
		st::groupCallMemberActiveIcon->c.greenF(),
		st::groupCallMemberActiveIcon->c.blueF(),
		st::groupCallMemberActiveIcon->c.alphaF() * outline));

	const auto shadowHeight = st.shadowHeight * _factor;
	const auto shadowAlpha = kShadowMaxAlpha / 255.f;
	program->setUniformValue(
		"shadow",
		QVector3D(shadowHeight, shown, shadowAlpha));
	program->setUniformValue("paused", GLfloat(paused));

	f.glActiveTexture(_rgbaFrame ? GL_TEXTURE1 : GL_TEXTURE3);
	tileData.textures.bind(f, kFirstBlurPassTextureIndex);
	program->setUniformValue("b_texture", GLint(_rgbaFrame ? 1 : 3));
	f.glActiveTexture(_rgbaFrame ? GL_TEXTURE2 : GL_TEXTURE5);
	_noiseTexture.bind(f, 0);
	program->setUniformValue("n_texture", GLint(_rgbaFrame ? 2 : 5));
	program->setUniformValue(
		"texelOffset",
		GLfloat(1.f / blurSize.height()));
	GLint blurTexcoord = program->attributeLocation("b_texcoordIn");
	f.glVertexAttribPointer(
		blurTexcoord,
		2,
		GL_FLOAT,
		GL_FALSE,
		2 * sizeof(GLfloat),
		reinterpret_cast<const void*>(48 * sizeof(GLfloat)));
	f.glEnableVertexAttribArray(blurTexcoord);
	FillTexturedRectangle(f, program, 8);
	f.glDisableVertexAttribArray(blurTexcoord);

	const auto pinVisible = _owner->wide()
		&& (pin.geometry.bottom() > y);
	const auto nameVisible = (nameShift != fullNameShift);
	const auto pausedVisible = (paused > 0.);
	if (!nameVisible && !pinVisible && !pausedVisible) {
		return;
	}

	f.glEnable(GL_BLEND);
	f.glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	const auto guard = gsl::finally([&] {
		f.glDisable(GL_BLEND);
	});

	_imageProgram->bind();
	_imageProgram->setUniformValue("viewport", uniformViewport);
	_imageProgram->setUniformValue("s_texture", GLint(0));

	f.glActiveTexture(GL_TEXTURE0);
	_buttons.bind(f);

	// Paused icon.
	if (pausedVisible) {
		_imageProgram->setUniformValue("g_opacity", GLfloat(paused));
		FillTexturedRectangle(f, &*_imageProgram, 30);
	}
	_imageProgram->setUniformValue("g_opacity", GLfloat(1.f));

	// Pin.
	if (pinVisible) {
		FillTexturedRectangle(f, &*_imageProgram, 14);
		FillTexturedRectangle(f, &*_imageProgram, 18);
	}

	// Mute.
	if (nameVisible && !muteRect.empty()) {
		FillTexturedRectangle(f, &*_imageProgram, 22);
	}

	if (!nameVisible && !pausedVisible) {
		return;
	}

	_names.bind(f);

	// Name.
	if (nameVisible && !nameRect.empty()) {
		FillTexturedRectangle(f, &*_imageProgram, 26);
	}

	// Paused text.
	if (pausedVisible && _owner->wide()) {
		_imageProgram->setUniformValue("g_opacity", GLfloat(paused));
		FillTexturedRectangle(f, &*_imageProgram, 34);
	}
}

void Viewport::RendererGL::prepareObjects(
		QOpenGLFunctions &f,
		TileData &tileData,
		QSize blurSize) {
	if (!tileData.textures.created()) {
		tileData.textures.ensureCreated(f); // All are GL_LINEAR, except..
		tileData.textures.bind(f, kScaleForBlurTextureIndex);

		// kScaleForBlurTextureIndex is attached to framebuffer 0,
		// and is used to draw to framebuffer 1 of the same size.
		f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	tileData.framebuffers.ensureCreated(f);

	if (tileData.textureBlurSize == blurSize) {
		return;
	}
	tileData.textureBlurSize = blurSize;

	const auto create = [&](int framebufferIndex, int index) {
		tileData.textures.bind(f, index);
		f.glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RGB,
			blurSize.width(),
			blurSize.height(),
			0,
			GL_RGB,
			GL_UNSIGNED_BYTE,
			nullptr);

		tileData.framebuffers.bind(f, framebufferIndex);
		f.glFramebufferTexture2D(
			GL_FRAMEBUFFER,
			GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D,
			tileData.textures.id(index),
			0);
	};
	create(0, kScaleForBlurTextureIndex);
	create(1, kFirstBlurPassTextureIndex);
}

bool Viewport::RendererGL::isExpanded(
		not_null<VideoTile*> tile,
		QSize unscaled,
		QSize tileSize) const {
	return !tile->screencast()
		&& (!_owner->wide() || UseExpandForCamera(unscaled, tileSize));
}

float64 Viewport::RendererGL::countExpandRatio(
		not_null<VideoTile*> tile,
		QSize unscaled,
		const TileAnimation &animation) const {
	const auto expandedFrom = isExpanded(tile, unscaled, animation.from);
	const auto expandedTo = isExpanded(tile, unscaled, animation.to);
	return (expandedFrom && expandedTo)
		? 1.
		: (!expandedFrom && !expandedTo)
		? 0.
		: expandedFrom
		? (1. - animation.ratio)
		: animation.ratio;
}

void Viewport::RendererGL::bindFrame(
		QOpenGLFunctions &f,
		const Webrtc::FrameWithInfo &data,
		TileData &tileData,
		Program &program) {
	const auto imageIndex = _userpicFrame ? 0 : (data.index + 1);
	const auto upload = (tileData.trackIndex != imageIndex);
	tileData.trackIndex = imageIndex;
	if (_rgbaFrame) {
		ensureARGB32Program();
		program.argb32->bind();
		f.glActiveTexture(GL_TEXTURE0);
		tileData.textures.bind(f, 0);
		if (upload) {
			const auto &image = _userpicFrame
				? tileData.userpicFrame
				: data.original;
			const auto stride = image.bytesPerLine() / 4;
			const auto data = image.constBits();
			uploadTexture(
				f,
				Ui::GL::kFormatRGBA,
				Ui::GL::kFormatRGBA,
				image.size(),
				tileData.rgbaSize,
				stride,
				data);
			tileData.rgbaSize = image.size();
			tileData.textureSize = QSize();
		}
		program.argb32->setUniformValue("s_texture", GLint(0));
	} else {
		const auto yuv = data.yuv420;
		const auto format = Ui::GL::CurrentSingleComponentFormat();
		program.yuv420->bind();
		f.glActiveTexture(GL_TEXTURE0);
		tileData.textures.bind(f, 0);
		if (upload) {
			f.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			uploadTexture(
				f,
				format,
				format,
				yuv->size,
				tileData.textureSize,
				yuv->y.stride,
				yuv->y.data);
			tileData.textureSize = yuv->size;
			tileData.rgbaSize = QSize();
		}
		f.glActiveTexture(GL_TEXTURE1);
		tileData.textures.bind(f, 1);
		if (upload) {
			uploadTexture(
				f,
				format,
				format,
				yuv->chromaSize,
				tileData.textureChromaSize,
				yuv->u.stride,
				yuv->u.data);
		}
		f.glActiveTexture(GL_TEXTURE2);
		tileData.textures.bind(f, 2);
		if (upload) {
			uploadTexture(
				f,
				format,
				format,
				yuv->chromaSize,
				tileData.textureChromaSize,
				yuv->v.stride,
				yuv->v.data);
			tileData.textureChromaSize = yuv->chromaSize;
			f.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		}
		program.yuv420->setUniformValue("y_texture", GLint(0));
		program.yuv420->setUniformValue("u_texture", GLint(1));
		program.yuv420->setUniformValue("v_texture", GLint(2));
	}
}

void Viewport::RendererGL::uploadTexture(
		QOpenGLFunctions &f,
		GLint internalformat,
		GLint format,
		QSize size,
		QSize hasSize,
		int stride,
		const void *data) const {
	f.glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
	if (hasSize != size) {
		f.glTexImage2D(
			GL_TEXTURE_2D,
			0,
			internalformat,
			size.width(),
			size.height(),
			0,
			format,
			GL_UNSIGNED_BYTE,
			data);
	} else {
		f.glTexSubImage2D(
			GL_TEXTURE_2D,
			0,
			0,
			0,
			size.width(),
			size.height(),
			format,
			GL_UNSIGNED_BYTE,
			data);
	}
	f.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void Viewport::RendererGL::drawDownscalePass(
		QOpenGLFunctions &f,
		TileData &tileData) {
	tileData.framebuffers.bind(f, 0);

	const auto program = _rgbaFrame
		? &*_downscaleProgram.argb32
		: &*_downscaleProgram.yuv420;

	FillTexturedRectangle(f, program);
}

void Viewport::RendererGL::drawFirstBlurPass(
		QOpenGLFunctions &f,
		TileData &tileData,
		QSize blurSize) {
	tileData.framebuffers.bind(f, 1);

	_blurProgram->bind();
	f.glActiveTexture(GL_TEXTURE0);
	tileData.textures.bind(f, kScaleForBlurTextureIndex);

	_blurProgram->setUniformValue("b_texture", GLint(0));
	_blurProgram->setUniformValue(
		"texelOffset",
		GLfloat(1.f / blurSize.width()));

	FillTexturedRectangle(f, &*_blurProgram, 4);
}

Rect Viewport::RendererGL::transformRect(const Rect &raster) const {
	return TransformRect(raster, _viewport, _factor);
}

Rect Viewport::RendererGL::transformRect(const QRect &raster) const {
	return TransformRect(Rect(raster), _viewport, _factor);
}

void Viewport::RendererGL::ensureButtonsImage() {
	if (_buttons) {
		return;
	}
	const auto pinOnSize = VideoTile::PinInnerSize(true);
	const auto pinOffSize = VideoTile::PinInnerSize(false);
	const auto backSize = VideoTile::BackInnerSize();
	const auto muteSize = st::groupCallVideoCrossLine.icon.size();
	const auto pausedSize = st::groupCallPaused.size();

	const auto fullSize = QSize(
		std::max({
			pinOnSize.width(),
			pinOffSize.width(),
			backSize.width(),
			2 * muteSize.width(),
			pausedSize.width(),
		}),
		(pinOnSize.height()
			+ pinOffSize.height()
			+ backSize.height()
			+ muteSize.height()
			+ pausedSize.height()));
	const auto imageSize = fullSize * _factor;
	auto image = _buttons.takeImage();
	if (image.size() != imageSize) {
		image = QImage(imageSize, QImage::Format_ARGB32_Premultiplied);
	}
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(_factor);
	{
		auto p = Painter(&image);
		auto hq = PainterHighQualityEnabler(p);

		_pinOn = QRect(QPoint(), pinOnSize * _factor);
		VideoTile::PaintPinButton(
			p,
			true,
			0,
			0,
			fullSize.width(),
			&_pinBackground,
			&_pinIcon);

		const auto pinOffTop = pinOnSize.height();
		_pinOff = QRect(
			QPoint(0, pinOffTop) * _factor,
			pinOffSize * _factor);
		VideoTile::PaintPinButton(
			p,
			false,
			0,
			pinOnSize.height(),
			fullSize.width(),
			&_pinBackground,
			&_pinIcon);

		const auto backTop = pinOffTop + pinOffSize.height();
		_back = QRect(QPoint(0, backTop) * _factor, backSize * _factor);
		VideoTile::PaintBackButton(
			p,
			0,
			pinOnSize.height() + pinOffSize.height(),
			fullSize.width(),
			&_pinBackground);

		const auto muteTop = backTop + backSize.height();
		_muteOn = QRect(QPoint(0, muteTop) * _factor, muteSize * _factor);
		_muteIcon.paint(p, { 0, muteTop }, 1.);

		_muteOff = QRect(
			QPoint(muteSize.width(), muteTop) * _factor,
			muteSize * _factor);
		_muteIcon.paint(p, { muteSize.width(), muteTop }, 0.);

		const auto pausedTop = muteTop + muteSize.height();
		_paused = QRect(
			QPoint(0, pausedTop) * _factor,
			pausedSize * _factor);
		st::groupCallPaused.paint(p, 0, pausedTop, fullSize.width());
	}
	_buttons.setImage(std::move(image));
}

void Viewport::RendererGL::validateDatas() {
	const auto &tiles = _owner->_tiles;
	const auto &st = st::groupCallVideoTile;
	const auto count = int(tiles.size());
	const auto factor = cIntRetinaFactor();
	const auto nameHeight = st::semiboldFont->height * factor;
	const auto pausedText = tr::lng_group_call_video_paused(tr::now);
	const auto pausedBottom = nameHeight;
	const auto pausedWidth = st::semiboldFont->width(pausedText) * factor;
	struct Request {
		int index = 0;
		bool updating = false;
	};
	auto requests = std::vector<Request>();
	auto available = std::max(_names.image().width(), pausedWidth);
	for (auto &data : _tileData) {
		data.stale = true;
	}
	_tileDataIndices.resize(count);
	const auto nameWidth = [&](int i) {
		const auto row = tiles[i]->row();
		const auto hasWidth = tiles[i]->geometry().width()
			- st.iconPosition.x()
			- st::groupCallVideoCrossLine.icon.width()
			- st.namePosition.x();
		if (hasWidth < 1) {
			return 0;
		}
		return std::clamp(row->name().maxWidth(), 1, hasWidth) * factor;
	};
	for (auto i = 0; i != count; ++i) {
		tiles[i]->row()->lazyInitialize(st::groupCallMembersListItem);
		const auto width = nameWidth(i);
		if (width > available) {
			available = width;
		}
		const auto id = quintptr(tiles[i]->track().get());
		const auto j = ranges::find(_tileData, id, &TileData::id);
		if (j != end(_tileData)) {
			j->stale = false;
			const auto index = (j - begin(_tileData));
			_tileDataIndices[i] = index;
			const auto peer = tiles[i]->row()->peer();
			if (peer != j->peer
				|| peer->nameVersion != j->nameVersion
				|| width != j->nameRect.width()) {
				const auto nameTop = pausedBottom + index * nameHeight;
				j->nameRect = QRect(0, nameTop, width, nameHeight);
				requests.push_back({ .index = i, .updating = true });
			}
		} else {
			_tileDataIndices[i] = -1;
			requests.push_back({ .index = i, .updating = false });
		}
	}
	if (requests.empty()) {
		return;
	}
	auto maybeStaleAfter = begin(_tileData);
	auto maybeStaleEnd = end(_tileData);
	for (auto &request : requests) {
		const auto i = request.index;
		if (_tileDataIndices[i] >= 0) {
			continue;
		}
		const auto id = quintptr(tiles[i]->track().get());
		const auto peer = tiles[i]->row()->peer();
		const auto paused = (tiles[i]->track()->state()
			== Webrtc::VideoState::Paused);
		auto index = int(_tileData.size());
		maybeStaleAfter = ranges::find(
			maybeStaleAfter,
			maybeStaleEnd,
			true,
			&TileData::stale);
		if (maybeStaleAfter != maybeStaleEnd) {
			index = (maybeStaleAfter - begin(_tileData));
			maybeStaleAfter->id = id;
			maybeStaleAfter->peer = peer;
			maybeStaleAfter->stale = false;
			maybeStaleAfter->pause = paused;
			maybeStaleAfter->paused.stop();
			request.updating = true;
		} else {
			// This invalidates maybeStale*, but they're already equal.
			_tileData.push_back({
				.id = id,
				.peer = peer,
				.pause = paused,
			});
		}
		const auto nameTop = pausedBottom + index * nameHeight;
		_tileData[index].nameVersion = peer->nameVersion;
		_tileData[index].nameRect = QRect(
			0,
			nameTop,
			nameWidth(i),
			nameHeight);
		_tileDataIndices[i] = index;
	}
	auto image = _names.takeImage();
	const auto imageSize = QSize(
		available,
		pausedBottom + _tileData.size() * nameHeight);
	const auto allocate = (image.size() != imageSize);
	auto paintToImage = allocate
		? QImage(imageSize, QImage::Format_ARGB32_Premultiplied)
		: base::take(image);
	paintToImage.setDevicePixelRatio(factor);
	if (allocate && image.isNull()) {
		paintToImage.fill(Qt::transparent);
	}
	{
		auto p = Painter(&paintToImage);
		p.setPen(st::groupCallVideoTextFg);
		if (!image.isNull()) {
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.drawImage(0, 0, image);
			if (paintToImage.width() > image.width()) {
				p.fillRect(
					image.width() / factor,
					0,
					(paintToImage.width() - image.width()) / factor,
					image.height() / factor,
					Qt::transparent);
			}
			if (paintToImage.height() > image.height()) {
				p.fillRect(
					0,
					image.height() / factor,
					paintToImage.width() / factor,
					(paintToImage.height() - image.height()) / factor,
					Qt::transparent);
			}
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		} else if (allocate) {
			p.setFont(st::semiboldFont);
			p.drawText(0, st::semiboldFont->ascent, pausedText);
			_pausedTextRect = QRect(0, 0, pausedWidth, nameHeight);
		}
		for (const auto &request : requests) {
			const auto i = request.index;
			const auto &data = _tileData[_tileDataIndices[i]];
			if (data.nameRect.isEmpty()) {
				continue;
			}
			const auto row = tiles[i]->row();
			if (request.updating) {
				p.setCompositionMode(QPainter::CompositionMode_Source);
				p.fillRect(
					0,
					data.nameRect.y() / factor,
					paintToImage.width() / factor,
					nameHeight / factor,
					Qt::transparent);
				p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			}
			row->name().drawLeftElided(
				p,
				0,
				data.nameRect.y() / factor,
				data.nameRect.width() / factor,
				paintToImage.width() / factor);
		}
	}
	_names.setImage(std::move(paintToImage));
}

void Viewport::RendererGL::validateNoiseTexture(
		QOpenGLFunctions &f,
		GLuint defaultFramebufferObject) {
	if (_noiseTexture.created()) {
		return;
	}
	const auto format = Ui::GL::CurrentSingleComponentFormat();
	_noiseTexture.ensureCreated(f, GL_NEAREST, GL_REPEAT);
	_noiseTexture.bind(f, 0);
	f.glTexImage2D(
		GL_TEXTURE_2D,
		0,
		format,
		kNoiseTextureSize,
		kNoiseTextureSize,
		0,
		format,
		GL_UNSIGNED_BYTE,
		nullptr);

	_noiseFramebuffer.ensureCreated(f);
	_noiseFramebuffer.bind(f, 0);

	f.glFramebufferTexture2D(
		GL_FRAMEBUFFER,
		GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D,
		_noiseTexture.id(0),
		0);

	f.glViewport(0, 0, kNoiseTextureSize, kNoiseTextureSize);

	const GLfloat coords[] = {
		-1, -1,
		-1,  1,
		 1,  1,
		 1, -1,
	};
	auto buffer = QOpenGLBuffer();
	buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
	buffer.create();
	buffer.bind();
	buffer.allocate(coords, sizeof(coords));

	auto program = QOpenGLShaderProgram();
	LinkProgram(
		&program,
		VertexShader({}),
		FragmentShader({ FragmentGenerateNoise() }));
	program.bind();

	GLint position = program.attributeLocation("position");
	f.glVertexAttribPointer(
		position,
		2,
		GL_FLOAT,
		GL_FALSE,
		2 * sizeof(GLfloat),
		nullptr);
	f.glEnableVertexAttribArray(position);

	f.glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	f.glDisableVertexAttribArray(position);

	f.glUseProgram(0);
}

void Viewport::RendererGL::validateOutlineAnimation(
		not_null<VideoTile*> tile,
		TileData &data) {
	const auto outline = tile->row()->speaking();
	if (data.outline == outline) {
		return;
	}
	data.outline = outline;
	data.outlined.start(
		[=] { _owner->widget()->update(); },
		outline ? 0. : 1.,
		outline ? 1. : 0.,
		st::fadeWrapDuration);
}

void Viewport::RendererGL::validatePausedAnimation(
		not_null<VideoTile*> tile,
		TileData &data) {
	const auto paused = (_userpicFrame
		&& tile->track()->frameSize().isEmpty())
		|| (tile->track()->state() == Webrtc::VideoState::Paused);
	if (data.pause == paused) {
		return;
	}
	data.pause = paused;
	data.paused.start(
		[=] { _owner->widget()->update(); },
		paused ? 0. : 1.,
		paused ? 1. : 0.,
		st::fadeWrapDuration);
}


} // namespace Calls::Group
