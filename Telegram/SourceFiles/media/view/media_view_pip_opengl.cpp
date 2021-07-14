/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_pip_opengl.h"

#include "ui/gl/gl_shader.h"
#include "ui/gl/gl_primitives.h"
#include "ui/widgets/shadow.h"
#include "media/streaming/media_streaming_common.h"
#include "base/platform/base_platform_info.h"
#include "styles/style_media_view.h"
#include "styles/style_calls.h" // st::callShadow.

namespace Media::View {
namespace {

using namespace Ui::GL;

constexpr auto kRadialLoadingOffset = 4;
constexpr auto kPlaybackOffset = kRadialLoadingOffset + 4;
constexpr auto kVolumeControllerOffset = kPlaybackOffset + 4;
constexpr auto kControlsOffset = kVolumeControllerOffset + 4;
constexpr auto kControlValues = 4 * 4 + 2 * 4;

[[nodiscard]] ShaderPart FragmentAddControlOver() {
	return {
		.header = R"(
varying vec2 o_texcoord;
uniform float o_opacity;
)",
		.body = R"(
	vec4 over = texture2D(s_texture, o_texcoord);
	result = result * (1. - o_opacity)
		+ vec4(over.b, over.g, over.r, over.a) * o_opacity;
)",
	};
}

[[nodiscard]] ShaderPart FragmentApplyFade() {
	return {
		.header = R"(
uniform vec4 fadeColor; // Premultiplied.
)",
		.body = R"(
	result = result * (1. - fadeColor.a) + fadeColor;
)",
	};
}

ShaderPart FragmentSampleShadow() {
	return {
		.header = R"(
uniform sampler2D h_texture;
uniform vec2 h_size;
uniform vec4 h_extend;
uniform vec4 h_components;
)",
		.body = R"(
	vec4 extended = vec4( // Left-Bottom-Width-Height rectangle.
		roundRect.xy - h_extend.xw,
		roundRect.zw + h_extend.xw + h_extend.zy);
	vec2 inside = (gl_FragCoord.xy - extended.xy);
	vec2 insideOtherCorner = (inside + h_size - extended.zw);
	vec4 outsideCorners = step(
		vec4(h_components.xy, inside),
		vec4(inside, extended.zw - h_components.xy));
	vec4 insideCorners = vec4(1.) - outsideCorners;
	vec2 linear = outsideCorners.xy * outsideCorners.zw;
	vec2 h_size_half = 0.5 * h_size;

	vec2 bottomleft = inside * insideCorners.x * insideCorners.y;
	vec2 bottomright = vec2(insideOtherCorner.x, inside.y)
		* insideCorners.z
		* insideCorners.y;
	vec2 topright = insideOtherCorner * insideCorners.z * insideCorners.w;
	vec2 topleft = vec2(inside.x, insideOtherCorner.y)
		* insideCorners.x
		* insideCorners.w;

	vec2 left = vec2(inside.x, h_size_half.y)
		* step(inside.x, h_components.z)
		* linear.y;
	vec2 bottom = vec2(h_size_half.x, inside.y)
		* step(inside.y, h_components.w)
		* linear.x;
	vec2 right = vec2(insideOtherCorner.x, h_size_half.y)
		* step(h_size.x - h_components.z, insideOtherCorner.x)
		* linear.y;
	vec2 top = vec2(h_size_half.x, insideOtherCorner.y)
		* step(h_size.y - h_components.w, insideOtherCorner.y)
		* linear.x;

	vec2 uv = bottomleft
		+ bottomright
		+ topleft
		+ topright
		+ left
		+ bottom
		+ right
		+ top;
	result = texture2D(h_texture, uv / h_size);
)",
	};
}

ShaderPart FragmentRoundToShadow() {
	const auto shadow = FragmentSampleShadow();
	return {
		.header = R"(
uniform vec4 roundRect;
uniform float roundRadius;
)" + shadow.header + R"(

float roundedCorner() {
	vec2 rectHalf = roundRect.zw / 2.;
	vec2 rectCenter = roundRect.xy + rectHalf;
	vec2 fromRectCenter = abs(gl_FragCoord.xy - rectCenter);
	vec2 vectorRadius = vec2(roundRadius + 0.5, roundRadius + 0.5);
	vec2 fromCenterWithRadius = fromRectCenter + vectorRadius;
	vec2 fromRoundingCenter = max(fromCenterWithRadius, rectHalf)
		- rectHalf;
	float rounded = length(fromRoundingCenter) - roundRadius;

	return 1. - smoothstep(0., 1., rounded);
}

vec4 shadow() {
	vec4 result;

)" + shadow.body + R"(

	return result;
}
)",
		.body = R"(
	float round = roundedCorner();
	result = result * round + shadow() * (1. - round);
)",
	};
}

} // namespace

Pip::RendererGL::RendererGL(not_null<Pip*> owner)
: _owner(owner) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_radialImage.invalidate();
		_playbackImage.invalidate();
		_volumeControllerImage.invalidate();
		invalidateControls();
	}, _lifetime);
}

void Pip::RendererGL::init(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	constexpr auto kQuads = 8;
	constexpr auto kQuadVertices = kQuads * 4;
	constexpr auto kQuadValues = kQuadVertices * 4;
	constexpr auto kControlsValues = kControlsCount * kControlValues;
	constexpr auto kValues = kQuadValues + kControlsValues;

	_contentBuffer.emplace();
	_contentBuffer->setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_contentBuffer->create();
	_contentBuffer->bind();
	_contentBuffer->allocate(kValues * sizeof(GLfloat));

	_textures.ensureCreated(f);

	_argb32Program.emplace();
	_texturedVertexShader = LinkProgram(
		&*_argb32Program,
		VertexShader({
			VertexPassTextureCoord(),
		}),
		FragmentShader({
			FragmentSampleARGB32Texture(),
			FragmentApplyFade(),
			FragmentRoundToShadow(),
		})).vertex;

	_yuv420Program.emplace();
	LinkProgram(
		&*_yuv420Program,
		_texturedVertexShader,
		FragmentShader({
			FragmentSampleYUV420Texture(),
			FragmentApplyFade(),
			FragmentRoundToShadow(),
		}));

	_imageProgram.emplace();
	LinkProgram(
		&*_imageProgram,
		VertexShader({
			VertexViewportTransform(),
			VertexPassTextureCoord(),
		}),
		FragmentShader({
			FragmentSampleARGB32Texture(),
		}));

	_controlsProgram.emplace();
	LinkProgram(
		&*_controlsProgram,
		VertexShader({
			VertexViewportTransform(),
			VertexPassTextureCoord(),
			VertexPassTextureCoord('o'),
		}),
		FragmentShader({
			FragmentSampleARGB32Texture(),
			FragmentAddControlOver(),
			FragmentGlobalOpacity(),
		}));

	createShadowTexture();
}

void Pip::RendererGL::deinit(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_textures.destroy(f);
	_imageProgram = std::nullopt;
	_texturedVertexShader = nullptr;
	_argb32Program = std::nullopt;
	_yuv420Program = std::nullopt;
	_controlsProgram = std::nullopt;
	_contentBuffer = std::nullopt;
}

void Pip::RendererGL::createShadowTexture() {
	const auto &shadow = st::callShadow;
	const auto size = 2 * st::callShadow.topLeft.size()
		+ QSize(st::roundRadiusLarge, st::roundRadiusLarge);
	auto image = QImage(
		size * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(cRetinaFactor());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		Ui::Shadow::paint(
			p,
			QRect(QPoint(), size).marginsRemoved(shadow.extend),
			size.width(),
			shadow);
	}
	_shadowImage.setImage(std::move(image));
}

void Pip::RendererGL::paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	const auto factor = widget->devicePixelRatio();
	if (_factor != factor) {
		_factor = factor;
		_controlsImage.invalidate();
	}
	_blendingEnabled = false;
	_viewport = widget->size();
	_uniformViewport = QVector2D(
		_viewport.width() * _factor,
		_viewport.height() * _factor);
	_f = &f;
	_owner->paint(this);
	_f = nullptr;
}

std::optional<QColor> Pip::RendererGL::clearColor() {
	return QColor(0, 0, 0, 0);
}

void Pip::RendererGL::paintTransformedVideoFrame(
		ContentGeometry geometry) {
	const auto data = _owner->videoFrameWithInfo();
	if (data.format == Streaming::FrameFormat::None) {
		return;
	}
	geometry.rotation = (geometry.rotation + geometry.videoRotation) % 360;
	if (data.format == Streaming::FrameFormat::ARGB32) {
		Assert(!data.original.isNull());
		paintTransformedStaticContent(data.original, geometry);
		return;
	}
	Assert(data.format == Streaming::FrameFormat::YUV420);
	Assert(!data.yuv420->size.isEmpty());
	const auto yuv = data.yuv420;
	_yuv420Program->bind();

	const auto upload = (_trackFrameIndex != data.index);
	_trackFrameIndex = data.index;

	const auto format = Ui::GL::CurrentSingleComponentFormat();
	_f->glActiveTexture(GL_TEXTURE0);
	_textures.bind(*_f, 1);
	if (upload) {
		_f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		uploadTexture(
			format,
			format,
			yuv->size,
			_lumaSize,
			yuv->y.stride,
			yuv->y.data);
		_lumaSize = yuv->size;
	}
	_f->glActiveTexture(GL_TEXTURE1);
	_textures.bind(*_f, 2);
	if (upload) {
		uploadTexture(
			format,
			format,
			yuv->chromaSize,
			_chromaSize,
			yuv->u.stride,
			yuv->u.data);
	}
	_f->glActiveTexture(GL_TEXTURE2);
	_textures.bind(*_f, 3);
	if (upload) {
		uploadTexture(
			format,
			format,
			yuv->chromaSize,
			_chromaSize,
			yuv->v.stride,
			yuv->v.data);
		_chromaSize = yuv->chromaSize;
		_f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	}
	_yuv420Program->setUniformValue("y_texture", GLint(0));
	_yuv420Program->setUniformValue("u_texture", GLint(1));
	_yuv420Program->setUniformValue("v_texture", GLint(2));

	paintTransformedContent(&*_yuv420Program, geometry);
}

void Pip::RendererGL::paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry) {
	_argb32Program->bind();

	_f->glActiveTexture(GL_TEXTURE0);
	_textures.bind(*_f, 0);
	const auto cacheKey = image.cacheKey();
	const auto upload = (_cacheKey != cacheKey);
	if (upload) {
		_cacheKey = cacheKey;
		const auto stride = image.bytesPerLine() / 4;
		const auto data = image.constBits();
		uploadTexture(
			Ui::GL::kFormatRGBA,
			Ui::GL::kFormatRGBA,
			image.size(),
			_rgbaSize,
			stride,
			data);
		_rgbaSize = image.size();
	}
	_argb32Program->setUniformValue("s_texture", GLint(0));

	paintTransformedContent(&*_argb32Program, geometry);
}

void Pip::RendererGL::paintTransformedContent(
		not_null<QOpenGLShaderProgram*> program,
		ContentGeometry geometry) {
	std::array<std::array<GLfloat, 2>, 4> rect = { {
		{ { -1.f, 1.f } },
		{ { 1.f, 1.f } },
		{ { 1.f, -1.f } },
		{ { -1.f, -1.f } },
	} };
	if (const auto shift = (geometry.rotation / 90); shift != 0) {
		std::rotate(begin(rect), begin(rect) + shift, end(rect));
	}
	const auto xscale = 1.f / geometry.inner.width();
	const auto yscale = 1.f / geometry.inner.height();
	const GLfloat coords[] = {
		rect[0][0], rect[0][1],
		-geometry.inner.x() * xscale,
		-geometry.inner.y() * yscale,

		rect[1][0], rect[1][1],
		(geometry.outer.width() - geometry.inner.x()) * xscale,
		-geometry.inner.y() * yscale,

		rect[2][0], rect[2][1],
		(geometry.outer.width() - geometry.inner.x()) * xscale,
		(geometry.outer.height() - geometry.inner.y()) * yscale,

		rect[3][0], rect[3][1],
		-geometry.inner.x() * xscale,
		(geometry.outer.height() - geometry.inner.y()) * yscale,
	};

	_contentBuffer->write(0, coords, sizeof(coords));

	const auto rgbaFrame = _chromaSize.isEmpty();
	_f->glActiveTexture(rgbaFrame ? GL_TEXTURE1 : GL_TEXTURE3);
	_shadowImage.bind(*_f);

	const auto globalFactor = cIntRetinaFactor();
	const auto fadeAlpha = st::radialBg->c.alphaF() * geometry.fade;
	const auto roundRect = transformRect(RoundingRect(geometry));
	program->setUniformValue("roundRect", Uniform(roundRect));
	program->setUniformValue("h_texture", GLint(rgbaFrame ? 1 : 3));
	program->setUniformValue("h_size", QSizeF(_shadowImage.image().size()));
	program->setUniformValue("h_extend", QVector4D(
		st::callShadow.extend.left() * globalFactor,
		st::callShadow.extend.top() * globalFactor,
		st::callShadow.extend.right() * globalFactor,
		st::callShadow.extend.bottom() * globalFactor));
	program->setUniformValue("h_components", QVector4D(
		float(st::callShadow.topLeft.width() * globalFactor),
		float(st::callShadow.topLeft.height() * globalFactor),
		float(st::callShadow.left.width() * globalFactor),
		float(st::callShadow.top.height() * globalFactor)));
	program->setUniformValue(
		"roundRadius",
		GLfloat(st::roundRadiusLarge * _factor));
	program->setUniformValue("fadeColor", QVector4D(
		float(st::radialBg->c.redF() * fadeAlpha),
		float(st::radialBg->c.greenF() * fadeAlpha),
		float(st::radialBg->c.blueF() * fadeAlpha),
		float(fadeAlpha)));

	FillTexturedRectangle(*_f, &*program);
}

void Pip::RendererGL::uploadTexture(
		GLint internalformat,
		GLint format,
		QSize size,
		QSize hasSize,
		int stride,
		const void *data) const {
	_f->glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
	if (hasSize != size) {
		_f->glTexImage2D(
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
		_f->glTexSubImage2D(
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
	_f->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void Pip::RendererGL::paintRadialLoading(
		QRect inner,
		float64 controlsShown) {
	paintUsingRaster(_radialImage, inner, [&](Painter &&p) {
		// Raster renderer paints content, then radial loading, then fade.
		// Here we paint fade together with the content, so we should emulate
		// radial loading being under the fade.
		//
		// The loading background is the same color as the fade (radialBg),
		// so nothing should be done with it. But the fade should be added
		// to the radial loading line color (radialFg).
		const auto newInner = QRect(QPoint(), inner.size());
		const auto fg = st::radialFg->c;
		const auto fade = st::radialBg->c;
		const auto fadeAlpha = controlsShown * fade.alphaF();
		const auto fgAlpha = 1. - fadeAlpha;
		const auto color = (fadeAlpha == 0.) ? fg : QColor(
			int(std::round(fg.red() * fgAlpha + fade.red() * fadeAlpha)),
			int(std::round(fg.green() * fgAlpha + fade.green() * fadeAlpha)),
			int(std::round(fg.blue() * fgAlpha + fade.blue() * fadeAlpha)),
			fg.alpha());

		_owner->paintRadialLoadingContent(p, newInner, color);
	}, kRadialLoadingOffset, true);
}

void Pip::RendererGL::paintPlayback(QRect outer, float64 shown) {
	paintUsingRaster(_playbackImage, outer, [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintPlaybackContent(p, newOuter, shown);
	}, kPlaybackOffset, true);
}

void Pip::RendererGL::paintVolumeController(QRect outer, float64 shown) {
	paintUsingRaster(_volumeControllerImage, outer, [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintVolumeControllerContent(p, newOuter, shown);
	}, kVolumeControllerOffset, true);
}

void Pip::RendererGL::paintButtonsStart() {
	validateControls();
	_f->glActiveTexture(GL_TEXTURE0);
	_controlsImage.bind(*_f);
	toggleBlending(true);
}

void Pip::RendererGL::paintButton(
		const Button &button,
		int outerWidth,
		float64 shown,
		float64 over,
		const style::icon &icon,
		const style::icon &iconOver) {
	const auto tryIndex = [&](int stateIndex) -> std::optional<Control> {
		const auto result = ControlMeta(button.state, stateIndex);
		return (result.icon == &icon && result.iconOver == &iconOver)
			? std::make_optional(result)
			: std::nullopt;
	};
	const auto meta = tryIndex(0)
		? *tryIndex(0)
		: tryIndex(1)
		? *tryIndex(1)
		: *tryIndex(2);
	Assert(meta.icon == &icon && meta.iconOver == &iconOver);

	const auto offset = kControlsOffset + (meta.index * kControlValues) / 4;
	const auto iconRect = _controlsImage.texturedRect(
		button.icon,
		_controlsTextures[meta.index * 2 + 0]);
	const auto iconOverRect = _controlsImage.texturedRect(
		button.icon,
		_controlsTextures[meta.index * 2 + 1]);
	const auto iconGeometry = transformRect(iconRect.geometry);
	const GLfloat coords[] = {
		iconGeometry.left(), iconGeometry.top(),
		iconRect.texture.left(), iconRect.texture.bottom(),

		iconGeometry.right(), iconGeometry.top(),
		iconRect.texture.right(), iconRect.texture.bottom(),

		iconGeometry.right(), iconGeometry.bottom(),
		iconRect.texture.right(), iconRect.texture.top(),

		iconGeometry.left(), iconGeometry.bottom(),
		iconRect.texture.left(), iconRect.texture.top(),

		iconOverRect.texture.left(), iconOverRect.texture.bottom(),
		iconOverRect.texture.right(), iconOverRect.texture.bottom(),
		iconOverRect.texture.right(), iconOverRect.texture.top(),
		iconOverRect.texture.left(), iconOverRect.texture.top(),
	};
	_contentBuffer->write(
		offset * 4 * sizeof(GLfloat),
		coords,
		sizeof(coords));
	_controlsProgram->bind();
	_controlsProgram->setUniformValue("o_opacity", GLfloat(over));
	_controlsProgram->setUniformValue("g_opacity", GLfloat(shown));
	_controlsProgram->setUniformValue("viewport", _uniformViewport);

	GLint overTexcoord = _controlsProgram->attributeLocation("o_texcoordIn");
	_f->glVertexAttribPointer(
		overTexcoord,
		2,
		GL_FLOAT,
		GL_FALSE,
		2 * sizeof(GLfloat),
		reinterpret_cast<const void*>((offset + 4) * 4 * sizeof(GLfloat)));
	_f->glEnableVertexAttribArray(overTexcoord);
	FillTexturedRectangle(*_f, &*_controlsProgram, offset);
	_f->glDisableVertexAttribArray(overTexcoord);
}

auto Pip::RendererGL::ControlMeta(OverState control, int index)
-> Control {
	Expects(index >= 0);

	switch (control) {
	case OverState::Close: Assert(index < 1); return {
		0,
		&st::pipCloseIcon,
		&st::pipCloseIconOver,
	};
	case OverState::Enlarge: Assert(index < 1); return {
		1,
		&st::pipEnlargeIcon,
		&st::pipEnlargeIconOver,
	};
	case OverState::VolumeToggle: Assert(index < 3); return {
		(2 + index),
		(index == 0
			? &st::pipVolumeIcon0
			: (index == 1)
			? &st::pipVolumeIcon1
			: &st::pipVolumeIcon2),
		(index == 0
			? &st::pipVolumeIcon0Over
			: (index == 1)
			? &st::pipVolumeIcon1Over
			: &st::pipVolumeIcon2Over),
	};
	case OverState::Other: Assert(index < 2); return {
		(5 + index),
		(index ? &st::pipPauseIcon : &st::pipPlayIcon),
		(index ? &st::pipPauseIconOver : &st::pipPlayIconOver),
	};
	}
	Unexpected("Control value in Pip::RendererGL::ControlIndex.");
}

void Pip::RendererGL::validateControls() {
	if (!_controlsImage.image().isNull()) {
		return;
	}
	const auto metas = {
		ControlMeta(OverState::Close),
		ControlMeta(OverState::Enlarge),
		ControlMeta(OverState::VolumeToggle),
		ControlMeta(OverState::VolumeToggle, 1),
		ControlMeta(OverState::VolumeToggle, 2),
		ControlMeta(OverState::Other),
		ControlMeta(OverState::Other, 1),
	};
	auto maxWidth = 0;
	auto fullHeight = 0;
	for (const auto &meta : metas) {
		Assert(meta.icon->size() == meta.iconOver->size());
		maxWidth = std::max(meta.icon->width(), maxWidth);
		fullHeight += 2 * meta.icon->height();
	}
	auto image = QImage(
		QSize(maxWidth, fullHeight) * _factor,
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(_factor);
	{
		auto p = QPainter(&image);
		auto index = 0;
		auto height = 0;
		const auto paint = [&](not_null<const style::icon*> icon) {
			icon->paint(p, 0, height, maxWidth);
			_controlsTextures[index++] = QRect(
				QPoint(0, height) * _factor,
				icon->size() * _factor);
			height += icon->height();
		};
		for (const auto &meta : metas) {
			paint(meta.icon);
			paint(meta.iconOver);
		}
	}
	_controlsImage.setImage(std::move(image));
}

void Pip::RendererGL::invalidateControls() {
	_controlsImage.invalidate();
	ranges::fill(_controlsTextures, QRect());
}

void Pip::RendererGL::paintUsingRaster(
		Ui::GL::Image &image,
		QRect rect,
		Fn<void(Painter&&)> method,
		int bufferOffset,
		bool transparent) {
	auto raster = image.takeImage();
	const auto size = rect.size() * _factor;
	if (raster.width() < size.width() || raster.height() < size.height()) {
		raster = QImage(size, QImage::Format_ARGB32_Premultiplied);
		raster.setDevicePixelRatio(_factor);
		if (!transparent
			&& (raster.width() > size.width()
				|| raster.height() > size.height())) {
			raster.fill(Qt::transparent);
		}
	} else if (raster.devicePixelRatio() != _factor) {
		raster.setDevicePixelRatio(_factor);
	}

	if (transparent) {
		raster.fill(Qt::transparent);
	}
	method(Painter(&raster));

	_f->glActiveTexture(GL_TEXTURE0);

	image.setImage(std::move(raster), size);
	image.bind(*_f);

	const auto textured = image.texturedRect(rect, QRect(QPoint(), size));
	const auto geometry = transformRect(textured.geometry);
	const GLfloat coords[] = {
		geometry.left(), geometry.top(),
		textured.texture.left(), textured.texture.bottom(),

		geometry.right(), geometry.top(),
		textured.texture.right(), textured.texture.bottom(),

		geometry.right(), geometry.bottom(),
		textured.texture.right(), textured.texture.top(),

		geometry.left(), geometry.bottom(),
		textured.texture.left(), textured.texture.top(),
	};
	_contentBuffer->write(
		bufferOffset * 4 * sizeof(GLfloat),
		coords,
		sizeof(coords));

	_imageProgram->bind();
	_imageProgram->setUniformValue("viewport", _uniformViewport);
	_imageProgram->setUniformValue("s_texture", GLint(0));
	_imageProgram->setUniformValue("g_opacity", GLfloat(1));

	toggleBlending(transparent);
	FillTexturedRectangle(*_f, &*_imageProgram, bufferOffset);
}

void Pip::RendererGL::toggleBlending(bool enabled) {
	if (_blendingEnabled == enabled) {
		return;
	} else if (enabled) {
		_f->glEnable(GL_BLEND);
		_f->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		_f->glDisable(GL_BLEND);
	}
	_blendingEnabled = enabled;
}

QRect Pip::RendererGL::RoundingRect(ContentGeometry geometry) {
	const auto inner = geometry.inner;
	const auto attached = geometry.attached;
	const auto added = std::max({
		st::roundRadiusLarge,
		inner.x(),
		inner.y(),
		geometry.outer.width() - inner.x() - inner.width(),
		geometry.outer.height() - inner.y() - inner.height(),
		st::callShadow.topLeft.width(),
		st::callShadow.topLeft.height(),
		st::callShadow.topRight.width(),
		st::callShadow.topRight.height(),
		st::callShadow.bottomRight.width(),
		st::callShadow.bottomRight.height(),
		st::callShadow.bottomLeft.width(),
		st::callShadow.bottomLeft.height(),
	});
	return geometry.inner.marginsAdded({
		(attached & RectPart::Left) ? added : 0,
		(attached & RectPart::Top) ? added : 0,
		(attached & RectPart::Right) ? added : 0,
		(attached & RectPart::Bottom) ? added : 0,
	});
}

Rect Pip::RendererGL::transformRect(const Rect &raster) const {
	return TransformRect(raster, _viewport, _factor);
}

Rect Pip::RendererGL::transformRect(const QRectF &raster) const {
	return TransformRect(raster, _viewport, _factor);
}

Rect Pip::RendererGL::transformRect(const QRect &raster) const {
	return TransformRect(Rect(raster), _viewport, _factor);
}

} // namespace Media::View
