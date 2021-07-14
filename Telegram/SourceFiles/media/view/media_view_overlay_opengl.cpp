/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_overlay_opengl.h"

#include "ui/gl/gl_shader.h"
#include "media/streaming/media_streaming_common.h"
#include "base/platform/base_platform_info.h"
#include "styles/style_media_view.h"

namespace Media::View {
namespace {

using namespace Ui::GL;

constexpr auto kRadialLoadingOffset = 4;
constexpr auto kThemePreviewOffset = kRadialLoadingOffset + 4;
constexpr auto kDocumentBubbleOffset = kThemePreviewOffset + 4;
constexpr auto kSaveMsgOffset = kDocumentBubbleOffset + 4;
constexpr auto kFooterOffset = kSaveMsgOffset + 4;
constexpr auto kCaptionOffset = kFooterOffset + 4;
constexpr auto kGroupThumbsOffset = kCaptionOffset + 4;
constexpr auto kControlsOffset = kGroupThumbsOffset + 4;
constexpr auto kControlValues = 2 * 4 + 4 * 4;

[[nodiscard]] ShaderPart FragmentPlaceOnTransparentBackground() {
	return {
		.header = R"(
uniform vec4 transparentBg;
uniform vec4 transparentFg;
uniform float transparentSize;
)",
		.body = R"(
	vec2 checkboardLadder = floor(gl_FragCoord.xy / transparentSize);
	float checkboard = mod(checkboardLadder.x + checkboardLadder.y, 2.0);
	vec4 checkboardColor = checkboard * transparentBg
		+ (1. - checkboard) * transparentFg;
	result += checkboardColor * (1. - result.a);
)",
	};
}

} // namespace

OverlayWidget::RendererGL::RendererGL(not_null<OverlayWidget*> owner)
: _owner(owner) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_radialImage.invalidate();
		_documentBubbleImage.invalidate();
		_themePreviewImage.invalidate();
		_saveMsgImage.invalidate();
		_footerImage.invalidate();
		_captionImage.invalidate();
		invalidateControls();
	}, _lifetime);
}

void OverlayWidget::RendererGL::init(
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

	_imageProgram.emplace();
	_texturedVertexShader = LinkProgram(
		&*_imageProgram,
		VertexShader({
			VertexViewportTransform(),
			VertexPassTextureCoord(),
		}),
		FragmentShader({
			FragmentSampleARGB32Texture(),
		})).vertex;

	_withTransparencyProgram.emplace();
	LinkProgram(
		&*_withTransparencyProgram,
		_texturedVertexShader,
		FragmentShader({
			FragmentSampleARGB32Texture(),
			FragmentPlaceOnTransparentBackground(),
		}));

	_yuv420Program.emplace();
	LinkProgram(
		&*_yuv420Program,
		_texturedVertexShader,
		FragmentShader({
			FragmentSampleYUV420Texture(),
		}));

	_fillProgram.emplace();
	LinkProgram(
		&*_fillProgram,
		VertexShader({ VertexViewportTransform() }),
		FragmentShader({ FragmentStaticColor() }));

	_controlsProgram.emplace();
	LinkProgram(
		&*_controlsProgram,
		_texturedVertexShader,
		FragmentShader({
			FragmentSampleARGB32Texture(),
			FragmentGlobalOpacity(),
		}));
}

void OverlayWidget::RendererGL::deinit(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_textures.destroy(f);
	_imageProgram = std::nullopt;
	_texturedVertexShader = nullptr;
	_withTransparencyProgram = std::nullopt;
	_yuv420Program = std::nullopt;
	_fillProgram = std::nullopt;
	_controlsProgram = std::nullopt;
	_contentBuffer = std::nullopt;
}

void OverlayWidget::RendererGL::paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	if (handleHideWorkaround(f)) {
		return;
	}
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

std::optional<QColor> OverlayWidget::RendererGL::clearColor() {
	if (Platform::IsWindows() && _owner->_hideWorkaround) {
		return QColor(0, 0, 0, 0);
	} else if (_owner->_fullScreenVideo) {
		return st::mediaviewVideoBg->c;
	} else {
		return st::mediaviewBg->c;
	}
}

bool OverlayWidget::RendererGL::handleHideWorkaround(QOpenGLFunctions &f) {
	// This is needed on Windows,
	// because on reopen it blinks with the last shown content.
	return Platform::IsWindows() && _owner->_hideWorkaround;
}

void OverlayWidget::RendererGL::paintBackground() {
	_contentBuffer->bind();
}

void OverlayWidget::RendererGL::paintTransformedVideoFrame(
		ContentGeometry geometry) {
	const auto data = _owner->videoFrameWithInfo();
	if (data.format == Streaming::FrameFormat::None) {
		return;
	}
	if (data.format == Streaming::FrameFormat::ARGB32) {
		Assert(!data.original.isNull());
		paintTransformedStaticContent(data.original, geometry, false, false);
		return;
	}
	Assert(data.format == Streaming::FrameFormat::YUV420);
	Assert(!data.yuv420->size.isEmpty());
	const auto yuv = data.yuv420;
	_yuv420Program->bind();

	const auto upload = (_trackFrameIndex != data.index)
		|| (_streamedIndex != _owner->streamedIndex());
	const auto format = Ui::GL::CurrentSingleComponentFormat();
	_trackFrameIndex = data.index;
	_streamedIndex = _owner->streamedIndex();

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

	toggleBlending(false);
	paintTransformedContent(&*_yuv420Program, geometry);
}

void OverlayWidget::RendererGL::paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry,
		bool semiTransparent,
		bool fillTransparentBackground) {
	Expects(image.isNull()
		|| image.format() == QImage::Format_RGB32
		|| image.format() == QImage::Format_ARGB32_Premultiplied);

	if (geometry.rect.isEmpty()) {
		return;
	}

	auto &program = fillTransparentBackground
		? _withTransparencyProgram
		: _imageProgram;
	program->bind();
	if (fillTransparentBackground) {
		program->setUniformValue(
			"transparentBg",
			st::mediaviewTransparentBg->c);
		program->setUniformValue(
			"transparentFg",
			st::mediaviewTransparentFg->c);
		program->setUniformValue(
			"transparentSize",
			st::transparentPlaceholderSize * _factor);
	}

	_f->glActiveTexture(GL_TEXTURE0);
	_textures.bind(*_f, 0);
	const auto cacheKey = image.isNull() ? qint64(-1) : image.cacheKey();
	const auto upload = (_cacheKey != cacheKey);
	if (upload) {
		_cacheKey = cacheKey;
		if (image.isNull()) {
			// Upload transparent 2x2 texture.
			const auto stride = 2;
			const uint32_t data[4] = { 0 };
			uploadTexture(
				Ui::GL::kFormatRGBA,
				Ui::GL::kFormatRGBA,
				QSize(2, 2),
				_rgbaSize,
				stride,
				data);
		} else {
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
	}
	program->setUniformValue("s_texture", GLint(0));

	toggleBlending(semiTransparent && !fillTransparentBackground);
	paintTransformedContent(&*program, geometry);
}

void OverlayWidget::RendererGL::paintTransformedContent(
		not_null<QOpenGLShaderProgram*> program,
		ContentGeometry geometry) {
	const auto rect = transformRect(geometry.rect);
	const auto centerx = rect.x() + rect.width() / 2;
	const auto centery = rect.y() + rect.height() / 2;
	const auto rsin = float(std::sin(geometry.rotation * M_PI / 180.));
	const auto rcos = float(std::cos(geometry.rotation * M_PI / 180.));
	const auto rotated = [&](float x, float y) -> std::array<float, 2> {
		x -= centerx;
		y -= centery;
		return std::array<float, 2>{
			centerx + (x * rcos + y * rsin),
			centery + (y * rcos - x * rsin)
		};
	};
	const auto topleft = rotated(rect.left(), rect.top());
	const auto topright = rotated(rect.right(), rect.top());
	const auto bottomright = rotated(rect.right(), rect.bottom());
	const auto bottomleft = rotated(rect.left(), rect.bottom());
	const GLfloat coords[] = {
		topleft[0], topleft[1],
		0.f, 1.f,

		topright[0], topright[1],
		1.f, 1.f,

		bottomright[0], bottomright[1],
		1.f, 0.f,

		bottomleft[0], bottomleft[1],
		0.f, 0.f,
	};

	_contentBuffer->write(0, coords, sizeof(coords));

	program->setUniformValue("viewport", _uniformViewport);

	FillTexturedRectangle(*_f, &*program);
}

void OverlayWidget::RendererGL::uploadTexture(
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

void OverlayWidget::RendererGL::paintRadialLoading(
		QRect inner,
		bool radial,
		float64 radialOpacity) {
	paintUsingRaster(_radialImage, inner, [&](Painter &&p) {
		const auto newInner = QRect(QPoint(), inner.size());
		_owner->paintRadialLoadingContent(p, newInner, radial, radialOpacity);
	}, kRadialLoadingOffset, true);
}

void OverlayWidget::RendererGL::paintThemePreview(QRect outer) {
	paintUsingRaster(_themePreviewImage, outer, [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintThemePreviewContent(p, newOuter, newOuter);
	}, kThemePreviewOffset);
}

void OverlayWidget::RendererGL::paintDocumentBubble(
		QRect outer,
		QRect icon) {
	paintUsingRaster(_documentBubbleImage, outer, [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		const auto newIcon = icon.translated(-outer.topLeft());
		_owner->paintDocumentBubbleContent(p, newOuter, newIcon, newOuter);
	}, kDocumentBubbleOffset);
	_owner->paintRadialLoading(this);
}

void OverlayWidget::RendererGL::paintSaveMsg(QRect outer) {
	paintUsingRaster(_saveMsgImage, outer, [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintSaveMsgContent(p, newOuter, newOuter);
	}, kSaveMsgOffset, true);
}

void OverlayWidget::RendererGL::paintControlsStart() {
	validateControls();
	_f->glActiveTexture(GL_TEXTURE0);
	_controlsImage.bind(*_f);
	toggleBlending(true);
}

void OverlayWidget::RendererGL::paintControl(
		OverState control,
		QRect outer,
		float64 outerOpacity,
		QRect inner,
		float64 innerOpacity,
		const style::icon &icon) {
	const auto meta = ControlMeta(control);
	Assert(meta.icon == &icon);

	const auto &bg = st::mediaviewControlBg->c;
	const auto bgAlpha = int(std::round(bg.alpha() * outerOpacity));
	const auto offset = kControlsOffset + (meta.index * kControlValues) / 4;
	const auto fgOffset = offset + 2;
	const auto bgRect = transformRect(outer);
	const auto iconRect = _controlsImage.texturedRect(
		inner,
		_controlsTextures[meta.index]);
	const auto iconGeometry = transformRect(iconRect.geometry);
	const GLfloat coords[] = {
		bgRect.left(), bgRect.top(),
		bgRect.right(), bgRect.top(),
		bgRect.right(), bgRect.bottom(),
		bgRect.left(), bgRect.bottom(),

		iconGeometry.left(), iconGeometry.top(),
		iconRect.texture.left(), iconRect.texture.bottom(),

		iconGeometry.right(), iconGeometry.top(),
		iconRect.texture.right(), iconRect.texture.bottom(),

		iconGeometry.right(), iconGeometry.bottom(),
		iconRect.texture.right(), iconRect.texture.top(),

		iconGeometry.left(), iconGeometry.bottom(),
		iconRect.texture.left(), iconRect.texture.top(),
	};
	if (!outer.isEmpty() && bgAlpha > 0) {
		_contentBuffer->write(
			offset * 4 * sizeof(GLfloat),
			coords,
			sizeof(coords));
		_fillProgram->bind();
		_fillProgram->setUniformValue("viewport", _uniformViewport);
		FillRectangle(
			*_f,
			&*_fillProgram,
			offset,
			QColor(bg.red(), bg.green(), bg.blue(), bgAlpha));
	} else {
		_contentBuffer->write(
			fgOffset * 4 * sizeof(GLfloat),
			coords + (fgOffset - offset) * 4,
			sizeof(coords) - (fgOffset - offset) * 4 * sizeof(GLfloat));
	}
	_controlsProgram->bind();
	_controlsProgram->setUniformValue("g_opacity", GLfloat(innerOpacity));
	_controlsProgram->setUniformValue("viewport", _uniformViewport);
	FillTexturedRectangle(*_f, &*_controlsProgram, fgOffset);
}

auto OverlayWidget::RendererGL::ControlMeta(OverState control)
-> Control {
	switch (control) {
	case OverLeftNav: return { 0, &st::mediaviewLeft };
	case OverRightNav: return { 1, &st::mediaviewRight };
	case OverClose: return { 2, &st::mediaviewClose };
	case OverSave: return { 3, &st::mediaviewSave };
	case OverRotate: return { 4, &st::mediaviewRotate };
	case OverMore: return { 5, &st::mediaviewMore };
	}
	Unexpected("Control value in OverlayWidget::RendererGL::ControlIndex.");
}

void OverlayWidget::RendererGL::validateControls() {
	if (!_controlsImage.image().isNull()) {
		return;
	}
	const auto metas = {
		ControlMeta(OverLeftNav),
		ControlMeta(OverRightNav),
		ControlMeta(OverClose),
		ControlMeta(OverSave),
		ControlMeta(OverRotate),
		ControlMeta(OverMore),
	};
	auto maxWidth = 0;
	auto fullHeight = 0;
	for (const auto &meta : metas) {
		maxWidth = std::max(meta.icon->width(), maxWidth);
		fullHeight += meta.icon->height();
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
		for (const auto &meta : metas) {
			meta.icon->paint(p, 0, height, maxWidth);
			_controlsTextures[index++] = QRect(
				QPoint(0, height) * _factor,
				meta.icon->size() * _factor);
			height += meta.icon->height();
		}
	}
	_controlsImage.setImage(std::move(image));
}

void OverlayWidget::RendererGL::invalidateControls() {
	_controlsImage.invalidate();
	ranges::fill(_controlsTextures, QRect());
}

void OverlayWidget::RendererGL::paintFooter(QRect outer, float64 opacity) {
	paintUsingRaster(_footerImage, outer, [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintFooterContent(p, newOuter, newOuter, opacity);
	}, kFooterOffset, true);
}

void OverlayWidget::RendererGL::paintCaption(QRect outer, float64 opacity) {
	paintUsingRaster(_captionImage, outer, [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintCaptionContent(p, newOuter, newOuter, opacity);
	}, kCaptionOffset, true);
}

void OverlayWidget::RendererGL::paintGroupThumbs(
		QRect outer,
		float64 opacity) {
	paintUsingRaster(_groupThumbsImage, outer, [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintGroupThumbsContent(p, newOuter, newOuter, opacity);
	}, kGroupThumbsOffset, true);
}

void OverlayWidget::RendererGL::invalidate() {
	_trackFrameIndex = -1;
	_streamedIndex = -1;
	const auto images = {
		&_radialImage,
		&_documentBubbleImage,
		&_themePreviewImage,
		&_saveMsgImage,
		&_footerImage,
		&_captionImage,
		&_groupThumbsImage,
		&_controlsImage,
	};
	for (const auto image : images) {
		image->setImage(QImage());
	}
	invalidateControls();
}

void OverlayWidget::RendererGL::paintUsingRaster(
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

	toggleBlending(transparent);
	FillTexturedRectangle(*_f, &*_imageProgram, bufferOffset);
}

void OverlayWidget::RendererGL::toggleBlending(bool enabled) {
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

Rect OverlayWidget::RendererGL::transformRect(const Rect &raster) const {
	return TransformRect(raster, _viewport, _factor);
}

Rect OverlayWidget::RendererGL::transformRect(const QRectF &raster) const {
	return TransformRect(raster, _viewport, _factor);
}

Rect OverlayWidget::RendererGL::transformRect(const QRect &raster) const {
	return TransformRect(Rect(raster), _viewport, _factor);
}

} // namespace Media::View
