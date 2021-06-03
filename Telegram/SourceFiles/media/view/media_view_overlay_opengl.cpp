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

namespace Media::View {
namespace {

using namespace Ui::GL;

constexpr auto kQuads = 8;
constexpr auto kQuadVertices = kQuads * 4;
constexpr auto kQuadValues = kQuadVertices * 4;
constexpr auto kControls = 6;
constexpr auto kControlValues = 2 * 4 + 4 * 4;
constexpr auto kControlsValues = kControls * kControlValues;
constexpr auto kValues = kQuadValues + kControlsValues;

constexpr auto kRadialLoadingOffset = 4;
constexpr auto kThemePreviewOffset = kRadialLoadingOffset + 4;
constexpr auto kDocumentBubbleOffset = kThemePreviewOffset + 4;
constexpr auto kSaveMsgOffset = kDocumentBubbleOffset + 4;
constexpr auto kFooterOffset = kSaveMsgOffset + 4;
constexpr auto kCaptionOffset = kFooterOffset + 4;
constexpr auto kGroupThumbsOffset = kCaptionOffset + 4;

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
	}, _lifetime);
}

void OverlayWidget::RendererGL::init(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_factor = widget->devicePixelRatio();
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

	_background.init(f);
}

void OverlayWidget::RendererGL::deinit(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_background.deinit(f);
	_textures.destroy(f);
	_imageProgram = std::nullopt;
	_texturedVertexShader = nullptr;
	_withTransparencyProgram = std::nullopt;
	_yuv420Program = std::nullopt;
	_contentBuffer = std::nullopt;
}

void OverlayWidget::RendererGL::resize(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f,
		int w,
		int h) {
	_factor = widget->devicePixelRatio();
	_viewport = QSize(w, h);
	setDefaultViewport(f);
}

void OverlayWidget::RendererGL::setDefaultViewport(QOpenGLFunctions &f) {
	const auto size = _viewport * _factor;
	f.glViewport(0, 0, size.width(), size.height());
}

void OverlayWidget::RendererGL::paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	if (handleHideWorkaround(f)) {
		return;
	}
	_f = &f;
	_owner->paint(this);
	_f = nullptr;
}

bool OverlayWidget::RendererGL::handleHideWorkaround(QOpenGLFunctions &f) {
	if (!Platform::IsWindows() || !_owner->_hideWorkaround) {
		return false;
	}
	// This is needed on Windows,
	// because on reopen it blinks with the last shown content.
	f.glClearColor(0., 0., 0., 0.);
	f.glClear(GL_COLOR_BUFFER_BIT);
	return true;
}

void OverlayWidget::RendererGL::paintBackground() {
	const auto &bg = _owner->_fullScreenVideo
		? st::mediaviewVideoBg
		: st::mediaviewBg;
	auto fill = QRegion(QRect(QPoint(), _viewport));
	if (_owner->opaqueContentShown()) {
		fill -= _owner->contentRect();
	}
	toggleBlending(false);
	_background.fill(
		*_f,
		fill,
		_viewport,
		_factor,
		bg);
}

void OverlayWidget::RendererGL::paintTransformedVideoFrame(
		QRect rect,
		int rotation) {
	const auto data = _owner->videoFrameWithInfo();
	if (data.format == Streaming::FrameFormat::None) {
		return;
	}
	if (data.format == Streaming::FrameFormat::ARGB32) {
		Assert(!data.original.isNull());
		paintTransformedStaticContent(
			data.original,
			rect,
			rotation,
			false);
		return;
	}
	Assert(data.format == Streaming::FrameFormat::YUV420);
	Assert(!data.yuv420->size.isEmpty());
	const auto yuv = data.yuv420;
	_f->glUseProgram(_yuv420Program->programId());

	const auto upload = (_trackFrameIndex != data.index)
		|| (_streamedIndex != _owner->streamedIndex());
	_trackFrameIndex = data.index;
	_streamedIndex = _owner->streamedIndex();

	_f->glActiveTexture(GL_TEXTURE0);
	_textures.bind(*_f, 0);
	if (upload) {
		_f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		uploadTexture(
			GL_RED,
			GL_RED,
			yuv->size,
			_lumaSize,
			yuv->y.stride,
			yuv->y.data);
		_lumaSize = yuv->size;
		_rgbaSize = QSize();
	}
	_f->glActiveTexture(GL_TEXTURE1);
	_textures.bind(*_f, 1);
	if (upload) {
		uploadTexture(
			GL_RED,
			GL_RED,
			yuv->chromaSize,
			_chromaSize,
			yuv->u.stride,
			yuv->u.data);
	}
	_f->glActiveTexture(GL_TEXTURE2);
	_textures.bind(*_f, 2);
	if (upload) {
		uploadTexture(
			GL_RED,
			GL_RED,
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

	paintTransformedContent(&*_yuv420Program, rect, rotation);
}

void OverlayWidget::RendererGL::paintTransformedStaticContent(
		const QImage &image,
		QRect rect,
		int rotation,
		bool fillTransparentBackground) {
	auto &program = fillTransparentBackground
		? _withTransparencyProgram
		: _imageProgram;
	_f->glUseProgram(program->programId());
	if (fillTransparentBackground) {
		program->setUniformValue(
			"transparentBg",
			Uniform(st::mediaviewTransparentBg->c));
		program->setUniformValue(
			"transparentFg",
			Uniform(st::mediaviewTransparentFg->c));
		program->setUniformValue(
			"transparentSize",
			st::transparentPlaceholderSize * _factor);
	}

	_f->glActiveTexture(GL_TEXTURE0);
	_textures.bind(*_f, 0);
	const auto cacheKey = image.cacheKey();
	const auto upload = (_cacheKey != cacheKey);
	if (upload) {
		const auto stride = image.bytesPerLine() / 4;
		const auto data = image.constBits();
		uploadTexture(
			GL_RGBA,
			GL_RGBA,
			image.size(),
			_rgbaSize,
			stride,
			data);
		_rgbaSize = image.size();
		_lumaSize = QSize();
	}

	paintTransformedContent(&*program, rect, rotation);
}

void OverlayWidget::RendererGL::paintTransformedContent(
		not_null<QOpenGLShaderProgram*> program,
		QRect rect,
		int rotation) {
	auto texCoords = std::array<std::array<GLfloat, 2>, 4> { {
		{ { 0.f, 1.f } },
		{ { 1.f, 1.f } },
		{ { 1.f, 0.f } },
		{ { 0.f, 0.f } },
	} };
	if (const auto shift = (rotation / 90); shift > 0) {
		std::rotate(
			texCoords.begin(),
			texCoords.begin() + shift,
			texCoords.end());
	}

	const auto geometry = transformRect(rect);
	const GLfloat coords[] = {
		geometry.left(), geometry.top(),
		texCoords[0][0], texCoords[0][1],

		geometry.right(), geometry.top(),
		texCoords[1][0], texCoords[1][1],

		geometry.right(), geometry.bottom(),
		texCoords[2][0], texCoords[2][1],

		geometry.left(), geometry.bottom(),
		texCoords[3][0], texCoords[3][1],
	};

	_contentBuffer->bind();
	_contentBuffer->write(0, coords, sizeof(coords));

	program->setUniformValue("viewport", QSizeF(_viewport * _factor));
	program->setUniformValue("s_texture", GLint(0));

	toggleBlending(false);
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

void OverlayWidget::RendererGL::paintControl(
		OverState control,
		QRect outer,
		float64 outerOpacity,
		QRect inner,
		float64 innerOpacity,
		const style::icon &icon) {
	AssertIsDebug(controls);
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
	};
	for (const auto image : images) {
		image->setImage(QImage());
	}
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

	image.setImage(std::move(raster));
	image.bind(*_f, size);

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

	_f->glUseProgram(_imageProgram->programId());
	_imageProgram->setUniformValue("viewport", QSizeF(_viewport * _factor));
	_imageProgram->setUniformValue("s_texture", GLint(0));

	_f->glActiveTexture(GL_TEXTURE0);
	image.bind(*_f, size);

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

Rect OverlayWidget::RendererGL::transformRect(const QRect &raster) const {
	return TransformRect(Rect(raster), _viewport, _factor);
}

} // namespace Media::View
