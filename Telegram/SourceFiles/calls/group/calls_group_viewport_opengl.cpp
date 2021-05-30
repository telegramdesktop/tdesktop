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
#include "ui/gl/gl_shader.h"
#include "data/data_peer.h"
#include "styles/style_calls.h"

#include <QtGui/QOpenGLShader>

namespace Calls::Group {
namespace {

using namespace Ui::GL;

constexpr auto kScaleForBlurTextureIndex = 3;
constexpr auto kFirstBlurPassTextureIndex = 4;
constexpr auto kBlurTextureSizeFactor = 1.7;
constexpr auto kBlurOpacity = 0.7;
constexpr auto kMinCameraVisiblePart = 0.75;

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

// Depends on FragmetSampleTexture().
[[nodiscard]] ShaderPart FragmentFrameColor() {
	const auto blur = FragmentBlurTexture(true, 'b');
	return {
		.header = R"(
uniform vec4 frameBg;
uniform vec3 shadow; // fullHeight, shown, maxOpacity
const float backgroundOpacity = )" + QString::number(kBlurOpacity) + R"(;
float insideTexture() {
	vec2 textureHalf = vec2(0.5, 0.5);
	vec2 fromTextureCenter = abs(v_texcoord - textureHalf);
	vec2 fromTextureEdge = max(fromTextureCenter, textureHalf) - textureHalf;
	float outsideCheck = dot(fromTextureEdge, fromTextureEdge);
	return step(outsideCheck, 0);
}
)" + blur.header + R"(
vec4 background() {
	vec4 result;
)" + blur.body + R"(
	return result;
}
)",
		.body = R"(
	float inside = insideTexture();
	result = result * inside
		+ (1. - inside) * (backgroundOpacity * background()
			+ (1. - backgroundOpacity) * frameBg);

	float shadowCoord = gl_FragCoord.y - roundRect.y;
	float shadowValue = max(1. - (shadowCoord / shadow.x), 0.);
	float shadowShown = shadowValue * shadow.y * shadow.z;
	result = vec4(result.rgb * (1. - shadowShown), result.a);
)",
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
		QSize viewport,
		float factor) {
	factor *= kBlurTextureSizeFactor; // The more the scale - more blurred the image.
	const auto area = viewport / int(std::round(factor * cScale() / 100));
	const auto scaled = unscaled.scaled(area, Qt::KeepAspectRatio);
	return (scaled.width() > unscaled.width()
		|| scaled.height() > unscaled.height())
		? unscaled
		: NonEmpty(scaled);
}

[[nodiscard]] std::array<std::array<GLfloat, 2>, 4> CountTexCoords(
		QSize unscaled,
		QSize size,
		bool expand,
		bool swap = false) {
	const auto scaled = NonEmpty(unscaled.scaled(
		size,
		expand ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio));
	const auto left = (size.width() - scaled.width()) / 2;
	const auto top = (size.height() - scaled.height()) / 2;
	const auto right = left + scaled.width();
	const auto bottom = top + scaled.height();
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

void FillRectVertices(GLfloat *coords, Rect rect) {
	coords[0] = coords[10] = rect.left();
	coords[1] = coords[11] = rect.top();
	coords[2] = rect.right();
	coords[3] = rect.top();
	coords[4] = coords[6] = rect.right();
	coords[5] = coords[7] = rect.bottom();
	coords[8] = rect.left();
	coords[9] = rect.bottom();
}

void FillTriangles(
		QOpenGLFunctions &f,
		gsl::span<const GLfloat> coords,
		not_null<QOpenGLBuffer*> buffer,
		not_null<QOpenGLShaderProgram*> program,
		QSize viewportWithFactor,
		const QColor &color,
		Fn<void()> additional = nullptr) {
	Expects(coords.size() % 6 == 0);

	if (coords.empty()) {
		return;
	}
	buffer->bind();
	buffer->allocate(coords.data(), coords.size() * sizeof(GLfloat));

	f.glUseProgram(program->programId());
	program->setUniformValue("viewport", QSizeF(viewportWithFactor));
	program->setUniformValue("s_color", Uniform(color));

	GLint position = program->attributeLocation("position");
	f.glVertexAttribPointer(
		position,
		2,
		GL_FLOAT,
		GL_FALSE,
		2 * sizeof(GLfloat),
		nullptr);
	f.glEnableVertexAttribArray(position);

	if (additional) {
		additional();
	}

	f.glDrawArrays(GL_TRIANGLES, 0, coords.size() / 2);

	f.glDisableVertexAttribArray(position);
}

void FillTexturedRectangle(
		QOpenGLFunctions &f,
		not_null<QOpenGLShaderProgram*> program,
		int skipVertices = 0) {
	const auto shift = [&](int elements) {
		return reinterpret_cast<const void*>(
			(skipVertices * 4 + elements) * sizeof(GLfloat));
	};
	GLint position = program->attributeLocation("position");
	f.glVertexAttribPointer(
		position,
		2,
		GL_FLOAT,
		GL_FALSE,
		4 * sizeof(GLfloat),
		shift(0));
	f.glEnableVertexAttribArray(position);

	GLint texcoord = program->attributeLocation("v_texcoordIn");
	f.glVertexAttribPointer(
		texcoord,
		2,
		GL_FLOAT,
		GL_FALSE,
		4 * sizeof(GLfloat),
		shift(2));
	f.glEnableVertexAttribArray(texcoord);

	f.glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	f.glDisableVertexAttribArray(position);
	f.glDisableVertexAttribArray(texcoord);
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
	_factor = widget->devicePixelRatio();
	_frameBuffer.emplace();
	_frameBuffer->setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_frameBuffer->create();
	_frameBuffer->bind();
	constexpr auto kQuads = 7;
	constexpr auto kQuadVertices = kQuads * 4;
	constexpr auto kQuadValues = kQuadVertices * 4;
	constexpr auto kValues = kQuadValues + 8; // Blur texture coordinates.
	_frameBuffer->allocate(kValues * sizeof(GLfloat));
	_downscaleProgram.yuv420.emplace();
	_downscaleVertexShader = LinkProgram(
		&*_downscaleProgram.yuv420,
		VertexShader({
			VertexPassTextureCoord(),
		}),
		FragmentShader({
			FragmentSampleYUV420Texture(),
		})).vertex;
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
			FragmentRoundCorners(),
		})).vertex;

	_bgBuffer.emplace();
	_bgBuffer->setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_bgBuffer->create();
	_bgProgram.emplace();
	LinkProgram(
		&*_bgProgram,
		VertexShader({ VertexViewportTransform() }),
		FragmentShader({ FragmentStaticColor() }));

	_imageProgram.emplace();
	LinkProgram(
		&*_imageProgram,
		_frameVertexShader,
		FragmentShader({
			FragmentSampleARGB32Texture(),
		}));
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
			FragmentRoundCorners(),
		}));
}

void Viewport::RendererGL::deinit(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_bgBuffer = std::nullopt;
	_frameBuffer = std::nullopt;
	_frameVertexShader = nullptr;
	_bgProgram = std::nullopt;
	_imageProgram = std::nullopt;
	_downscaleProgram.argb32 = std::nullopt;
	_downscaleProgram.yuv420 = std::nullopt;
	_blurProgram = std::nullopt;
	_frameProgram.argb32 = std::nullopt;
	_frameProgram.yuv420 = std::nullopt;
	for (auto &data : _tileData) {
		data.textures.destroy(f);
	}
	_tileData.clear();
	_tileDataIndices.clear();
	_buttons.destroy(f);
}

void Viewport::RendererGL::resize(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f,
		int w,
		int h) {
	_factor = widget->devicePixelRatio();
	_viewport = QSize(w, h);
	setDefaultViewport(f);
}

void Viewport::RendererGL::setDefaultViewport(QOpenGLFunctions &f) {
	const auto size = _viewport * _factor;
	f.glViewport(0, 0, size.width(), size.height());
}

void Viewport::RendererGL::paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_factor = widget->devicePixelRatio();
	validateDatas();
	fillBackground(f);
	const auto defaultFramebufferObject = widget->defaultFramebufferObject();
	auto index = 0;
	for (const auto &tile : _owner->_tiles) {
		paintTile(
			f,
			defaultFramebufferObject,
			tile.get(),
			_tileData[_tileDataIndices[index++]]);
	}
}

void Viewport::RendererGL::fillBackground(QOpenGLFunctions &f) {
	const auto radius = st::roundRadiusLarge;
	const auto radiuses = QMargins{ radius, radius, radius, radius };
	auto bg = QRegion(QRect(QPoint(), _viewport));
	for (const auto &tile : _owner->_tiles) {
		bg -= tile->geometry().marginsRemoved(radiuses);
	}
	if (bg.isEmpty()) {
		return;
	}
	_bgTriangles.resize((bg.end() - bg.begin()) * 12);
	auto coords = _bgTriangles.data();
	for (const auto rect : bg) {
		FillRectVertices(coords, transformRect(rect));
		coords += 12;
	}
	FillTriangles(
		f,
		_bgTriangles,
		&*_bgBuffer,
		&*_bgProgram,
		_viewport * _factor,
		st::groupCallBg->c);
}

void Viewport::RendererGL::paintTile(
		QOpenGLFunctions &f,
		GLuint defaultFramebufferObject,
		not_null<VideoTile*> tile,
		TileData &tileData) {
	const auto track = tile->track();
	const auto data = track->frameWithInfo(false);
	if (data.format == Webrtc::FrameFormat::None) {
		return;
	}
	Assert(!data.yuv420->size.isEmpty());
	const auto geometry = tile->geometry();
	if (geometry.isEmpty()) {
		return;
	}

	_rgbaFrame = (data.format == Webrtc::FrameFormat::ARGB32);
	const auto x = geometry.x();
	const auto y = geometry.y();
	const auto width = geometry.width();
	const auto height = geometry.height();
	const auto &st = st::groupCallVideoTile;
	const auto shown = _owner->_controlsShownRatio;
	const auto fullNameShift = st.namePosition.y() + st::normalFont->height;
	const auto nameShift = anim::interpolate(fullNameShift, 0, shown);
	const auto row = tile->row();
	const auto style = row->computeIconState(MembersRowStyle::Video);

	validateOutlineAnimation(tile, tileData);
	const auto outline = tileData.outlined.value(tileData.outline ? 1. : 0.);

	ensureButtonsImage();

	// Frame.
	const auto unscaled = Media::View::FlipSizeByRotation(
		data.yuv420->size,
		data.rotation);
	const auto tileSize = geometry.size();
	const auto swap = (((data.rotation / 90) % 2) == 1);
	const auto expand = !tile->screencast()
		&& (!_owner->wide() || UseExpandForCamera(unscaled, tileSize));
	auto texCoords = CountTexCoords(unscaled, tileSize, expand, swap);
	auto blurTexCoords = expand
		? texCoords
		: CountTexCoords(unscaled, tileSize, true);
	const auto rect = transformRect(geometry);
	auto toBlurTexCoords = std::array<std::array<GLfloat, 2>, 4> { {
		{ { 0.f, 1.f } },
		{ { 1.f, 1.f } },
		{ { 1.f, 0.f } },
		{ { 0.f, 0.f } },
	} };
	if (const auto shift = (data.rotation / 90); shift > 0) {
		std::rotate(
			toBlurTexCoords.begin(),
			toBlurTexCoords.begin() + shift,
			toBlurTexCoords.end());
		std::rotate(
			texCoords.begin(),
			texCoords.begin() + shift,
			texCoords.end());
	}

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
		y + (height
			- st.namePosition.y()
			- st::semiboldFont->height
			+ nameShift));
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
	};

	const auto blurSize = CountBlurredSize(unscaled, _viewport, _factor);
	prepareObjects(f, tileData, blurSize);
	f.glViewport(0, 0, blurSize.width(), blurSize.height());

	_frameBuffer->bind();
	_frameBuffer->write(0, coords, sizeof(coords));

	bindFrame(f, data, tileData, _downscaleProgram);
	tile->track()->markFrameShown();

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
	program->setUniformValue("frameBg", Uniform(st::groupCallBg->c));
	program->setUniformValue("radiusOutline", QVector2D(
		GLfloat(st::roundRadiusLarge * _factor),
		(outline > 0) ? (st::groupCallOutline * _factor) : 0.f));
	program->setUniformValue("roundRect", Uniform(rect));
	program->setUniformValue("roundBg", Uniform(st::groupCallBg->c));
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

	f.glActiveTexture(_rgbaFrame ? GL_TEXTURE1 : GL_TEXTURE3);
	tileData.textures.bind(
		f,
		tileData.textureIndex * 5 + kFirstBlurPassTextureIndex);
	program->setUniformValue("b_texture", GLint(_rgbaFrame ? 1 : 3));
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
	if (nameShift == fullNameShift && !pinVisible) {
		return;
	}

	f.glEnable(GL_BLEND);
	f.glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	const auto guard = gsl::finally([&] {
		f.glDisable(GL_BLEND);
	});

	f.glUseProgram(_imageProgram->programId());
	_imageProgram->setUniformValue("viewport", uniformViewport);
	_imageProgram->setUniformValue("s_texture", GLint(0));

	f.glActiveTexture(GL_TEXTURE0);
	_buttons.bind(f);

	if (pinVisible) {
		FillTexturedRectangle(f, &*_imageProgram, 14);
		FillTexturedRectangle(f, &*_imageProgram, 18);
	}

	if (nameShift == fullNameShift) {
		return;
	}

	// Mute.
	if (!muteRect.empty()) {
		FillTexturedRectangle(f, &*_imageProgram, 22);
	}

	// Name.
	if (!nameRect.empty()) {
		_names.bind(f);
		FillTexturedRectangle(f, &*_imageProgram, 26);
	}
}

void Viewport::RendererGL::prepareObjects(
		QOpenGLFunctions &f,
		TileData &tileData,
		QSize blurSize) {
	tileData.textures.ensureCreated(f);
	tileData.framebuffers.ensureCreated(f);

	if (tileData.textureBlurSize == blurSize) {
		return;
	}
	tileData.textureBlurSize = blurSize;

	const auto create = [&](int framebufferIndex, int index) {
		index += tileData.textureIndex * 5;

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

void Viewport::RendererGL::bindFrame(
		QOpenGLFunctions &f,
		const Webrtc::FrameWithInfo &data,
		TileData &tileData,
		Program &program) {
	const auto upload = (tileData.trackIndex != data.index);
	tileData.trackIndex = data.index;
	const auto uploadOne = [&](
			GLint internalformat,
			GLint format,
			QSize size,
			QSize hasSize,
			int stride,
			const void *data) {
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
	};
	//if (upload) {
	//	tileData.textureIndex = 1 - tileData.textureIndex;
	//}
	if (_rgbaFrame) {
		ensureARGB32Program();
		f.glUseProgram(program.argb32->programId());
		f.glActiveTexture(GL_TEXTURE0);
		tileData.textures.bind(f, tileData.textureIndex);
		if (upload) {
			const auto &image = data.original;
			const auto stride = image.bytesPerLine() / 4;
			const auto data = image.constBits();
			uploadOne(
				GL_RGBA,
				GL_RGBA,
				image.size(),
				tileData.rgbaSize,
				stride,
				data);
			tileData.rgbaSize = image.size();
		}
		program.argb32->setUniformValue("s_texture", GLint(0));
	} else {
		const auto yuv = data.yuv420;
		f.glUseProgram(program.yuv420->programId());
		f.glActiveTexture(GL_TEXTURE0);
		tileData.textures.bind(f, tileData.textureIndex * 5 + 0);
		if (upload) {
			f.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			uploadOne(
				GL_RED,
				GL_RED,
				yuv->size,
				tileData.textureSize,
				yuv->y.stride,
				yuv->y.data);
			tileData.textureSize = yuv->size;
		}
		f.glActiveTexture(GL_TEXTURE1);
		tileData.textures.bind(f, tileData.textureIndex * 5 + 1);
		if (upload) {
			uploadOne(
				GL_RED,
				GL_RED,
				yuv->chromaSize,
				tileData.textureChromaSize,
				yuv->u.stride,
				yuv->u.data);
		}
		f.glActiveTexture(GL_TEXTURE2);
		tileData.textures.bind(f, tileData.textureIndex * 5 + 2);
		if (upload) {
			uploadOne(
				GL_RED,
				GL_RED,
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

	f.glUseProgram(_blurProgram->programId());
	f.glActiveTexture(GL_TEXTURE0);
	tileData.textures.bind(
		f,
		tileData.textureIndex * 5 + kScaleForBlurTextureIndex);

	_blurProgram->setUniformValue("b_texture", GLint(0));
	_blurProgram->setUniformValue(
		"texelOffset",
		GLfloat(1.f / blurSize.width()));

	FillTexturedRectangle(f, &*_blurProgram, 4);
}

Rect Viewport::RendererGL::transformRect(const Rect &raster) const {
	return {
		raster.left() * _factor,
		float(_viewport.height() - raster.bottom()) * _factor,
		raster.width() * _factor,
		raster.height() * _factor,
	};
}

Rect Viewport::RendererGL::transformRect(const QRect &raster) const {
	return {
		raster.x() * _factor,
		(_viewport.height() - raster.y() - raster.height()) * _factor,
		raster.width() * _factor,
		raster.height() * _factor,
	};
}

void Viewport::RendererGL::ensureButtonsImage() {
	if (_buttons) {
		return;
	}
	const auto factor = cIntRetinaFactor();
	const auto pinOnSize = VideoTile::PinInnerSize(true);
	const auto pinOffSize = VideoTile::PinInnerSize(false);
	const auto backSize = VideoTile::BackInnerSize();
	const auto muteSize = st::groupCallVideoCrossLine.icon.size();

	const auto fullSize = QSize(
		std::max({
			pinOnSize.width(),
			pinOffSize.width(),
			backSize.width(),
			2 * muteSize.width(),
		}),
		(pinOnSize.height()
			+ pinOffSize.height()
			+ backSize.height()
			+ muteSize.height()));
	const auto imageSize = fullSize * factor;
	auto image = _buttons.takeImage();
	if (image.size() != imageSize) {
		image = QImage(imageSize, QImage::Format_ARGB32_Premultiplied);
	}
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(cRetinaFactor());
	{
		auto p = Painter(&image);
		auto hq = PainterHighQualityEnabler(p);

		_pinOn = QRect(QPoint(), pinOnSize * factor);
		VideoTile::PaintPinButton(
			p,
			true,
			0,
			0,
			fullSize.width(),
			&_pinBackground,
			&_pinIcon);

		_pinOff = QRect(
			QPoint(0, pinOnSize.height()) * factor,
			pinOffSize * factor);
		VideoTile::PaintPinButton(
			p,
			false,
			0,
			pinOnSize.height(),
			fullSize.width(),
			&_pinBackground,
			&_pinIcon);

		_back = QRect(
			QPoint(0, pinOnSize.height() + pinOffSize.height()) * factor,
			backSize * factor);
		VideoTile::PaintBackButton(
			p,
			0,
			pinOnSize.height() + pinOffSize.height(),
			fullSize.width(),
			&_pinBackground);

		const auto muteTop = pinOnSize.height()
			+ pinOffSize.height()
			+ backSize.height();
		_muteOn = QRect(QPoint(0, muteTop) * factor, muteSize * factor);
		_muteIcon.paint(p, { 0, muteTop }, 1.);

		_muteOff = QRect(
			QPoint(muteSize.width(), muteTop) * factor,
			muteSize * factor);
		_muteIcon.paint(p, { muteSize.width(), muteTop }, 0.);
	}
	_buttons.setImage(std::move(image));
}

void Viewport::RendererGL::validateDatas() {
	const auto &tiles = _owner->_tiles;
	const auto &st = st::groupCallVideoTile;
	const auto count = int(tiles.size());
	const auto factor = cIntRetinaFactor();
	const auto nameHeight = st::semiboldFont->height * factor;
	struct Request {
		int index = 0;
		bool updating = false;
	};
	auto requests = std::vector<Request>();
	auto available = _names.image().width();
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
		return std::clamp(row->name().maxWidth(), 1, hasWidth) * factor;
	};
	for (auto i = 0; i != count; ++i) {
		tiles[i]->row()->lazyInitialize(st::groupCallMembersListItem);
		const auto width = nameWidth(i);
		if (width <= 0) {
			continue;
		}
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
				const auto nameTop = index * nameHeight;
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
			request.updating = true;
		} else {
			// This invalidates maybeStale*, but they're already equal.
			_tileData.push_back({
				.id = id,
				.peer = peer,
			});
		}
		_tileData[index].nameVersion = peer->nameVersion;
		_tileData[index].nameRect = QRect(
			0,
			index * nameHeight,
			nameWidth(i),
			nameHeight);
		_tileDataIndices[i] = index;
	}
	auto image = _names.takeImage();
	const auto imageSize = QSize(
		available * factor,
		_tileData.size() * nameHeight);
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
		}
		p.setPen(st::groupCallVideoTextFg);
		for (const auto &request : requests) {
			const auto i = request.index;
			const auto index = _tileDataIndices[i];
			const auto &data = _tileData[_tileDataIndices[i]];
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

} // namespace Calls::Group
