/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_overlay_opengl.h"

#include "data/data_peer_values.h" // AmPremiumValue.
#include "ui/gl/gl_shader.h"
#include "ui/painter.h"
#include "media/stories/media_stories_view.h"
#include "media/streaming/media_streaming_common.h"
#include "platform/platform_overlay_widget.h"
#include "base/platform/base_platform_info.h"
#include "core/crash_reports.h"
#include "styles/style_media_view.h"

namespace Media::View {
namespace {

using namespace Ui::GL;

constexpr auto kNotchOffset = 4;
constexpr auto kRadialLoadingOffset = kNotchOffset + 4;
constexpr auto kThemePreviewOffset = kRadialLoadingOffset + 4;
constexpr auto kDocumentBubbleOffset = kThemePreviewOffset + 4;
constexpr auto kSaveMsgOffset = kDocumentBubbleOffset + 4;
constexpr auto kFooterOffset = kSaveMsgOffset + 4;
constexpr auto kCaptionOffset = kFooterOffset + 4;
constexpr auto kGroupThumbsOffset = kCaptionOffset + 4;
constexpr auto kControlsOffset = kGroupThumbsOffset + 4;
constexpr auto kControlValues = 4 * 4 + 4 * 4; // over + icon

[[nodiscard]] ShaderPart FragmentApplyControlsFade() {
	return {
		.header = R"(
uniform sampler2D f_texture;
uniform vec4 shadowTopRect;
uniform vec4 shadowBottomSkipOpacityFullFade;
)",
		.body = R"(
	float topHeight = shadowTopRect.w;
	float bottomHeight = shadowBottomSkipOpacityFullFade.x;
	float bottomSkip = shadowBottomSkipOpacityFullFade.y;
	float opacity = shadowBottomSkipOpacityFullFade.z;
	float fullFade = shadowBottomSkipOpacityFullFade.w;
	float viewportHeight = shadowTopRect.y + topHeight;
	float fullHeight = topHeight + bottomHeight;
	float topY = min(
		(viewportHeight - gl_FragCoord.y) / fullHeight,
		topHeight / fullHeight);
	float topX = (gl_FragCoord.x - shadowTopRect.x) / shadowTopRect.z;
	vec4 fadeTop = texture2D(f_texture, vec2(topX, topY)) * opacity;
	float bottomY = max(bottomSkip + fullHeight - gl_FragCoord.y, topHeight)
		/ fullHeight;
	vec4 fadeBottom = texture2D(f_texture, vec2(0.5, bottomY)) * opacity;
	float fade = min((1. - fadeTop.a) * (1. - fadeBottom.a), fullFade);
	result.rgb = result.rgb * fade;
)",
	};
}

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

[[nodiscard]] ShaderPart FragmentRoundedCorners() {
	return {
		.header = R"(
uniform vec4 roundRect;
uniform float roundRadius;

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
)",
		.body = R"(
	result *= roundedCorner();
)",
	};
}

} // namespace

OverlayWidget::RendererGL::RendererGL(not_null<OverlayWidget*> owner)
: _owner(owner) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_controlsFadeImage.invalidate();
		_radialImage.invalidate();
		_documentBubbleImage.invalidate();
		_themePreviewImage.invalidate();
		_saveMsgImage.invalidate();
		_footerImage.invalidate();
		_captionImage.invalidate();
		invalidateControls();
	}, _lifetime);

	crl::on_main(this, [=] {
		_owner->_storiesChanged.events(
		) | rpl::start_with_next([=] {
			if (_owner->_storiesSession) {
				Data::AmPremiumValue(
					_owner->_storiesSession
				) | rpl::start_with_next([=] {
					invalidateControls();
				}, _storiesLifetime);
			} else {
				_storiesLifetime.destroy();
				invalidateControls();
			}
		}, _lifetime);
	});
}

void OverlayWidget::RendererGL::init(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	constexpr auto kQuads = 9;
	constexpr auto kQuadVertices = kQuads * 4;
	constexpr auto kQuadValues = kQuadVertices * 4;
	constexpr auto kControlsValues = kControlsCount * kControlValues;
	constexpr auto kRoundingQuads = 4;
	constexpr auto kRoundingVertices = kRoundingQuads * 6;
	constexpr auto kRoundingValues = kRoundingVertices * 2;
	constexpr auto kStoriesSiblingValues = kStoriesSiblingPartsCount * 16;
	constexpr auto kValues = kQuadValues
		+ kControlsValues
		+ kRoundingValues
		+ kStoriesSiblingValues;

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

	_staticContentProgram.emplace();
	LinkProgram(
		&*_staticContentProgram,
		_texturedVertexShader,
		FragmentShader({
			FragmentSampleARGB32Texture(),
			FragmentApplyControlsFade(),
			FragmentRoundedCorners()
		}));

	_withTransparencyProgram.emplace();
	LinkProgram(
		&*_withTransparencyProgram,
		_texturedVertexShader,
		FragmentShader({
			FragmentSampleARGB32Texture(),
			FragmentPlaceOnTransparentBackground(),
			FragmentApplyControlsFade()
		}));

	_yuv420Program.emplace();
	LinkProgram(
		&*_yuv420Program,
		_texturedVertexShader,
		FragmentShader({
			FragmentSampleYUV420Texture(),
			FragmentApplyControlsFade(),
			FragmentRoundedCorners()
		}));

	_nv12Program.emplace();
	LinkProgram(
		&*_nv12Program,
		_texturedVertexShader,
		FragmentShader({
			FragmentSampleNV12Texture(),
			FragmentApplyControlsFade(),
			FragmentRoundedCorners()
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

	_roundedCornersProgram.emplace();
	LinkProgram(
		&*_roundedCornersProgram,
		VertexShader({ VertexViewportTransform() }),
		FragmentShader({
			{ .body = "result = vec4(1.);" },
			FragmentRoundedCorners(),
		}));

	const auto renderer = reinterpret_cast<const char*>(
		f.glGetString(GL_RENDERER));
	CrashReports::SetAnnotation(
		"OpenGL Renderer",
		renderer ? renderer : "[nullptr]");
}

void OverlayWidget::RendererGL::deinit(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions *f) {
	_textures.destroy(f);
	_imageProgram = std::nullopt;
	_texturedVertexShader = nullptr;
	_withTransparencyProgram = std::nullopt;
	_yuv420Program = std::nullopt;
	_nv12Program = std::nullopt;
	_fillProgram = std::nullopt;
	_controlsProgram = std::nullopt;
	_contentBuffer = std::nullopt;
	_controlsFadeImage.destroy(f);
	_radialImage.destroy(f);
	_documentBubbleImage.destroy(f);
	_themePreviewImage.destroy(f);
	_saveMsgImage.destroy(f);
	_footerImage.destroy(f);
	_captionImage.destroy(f);
	_groupThumbsImage.destroy(f);
	_controlsImage.destroy(f);
	for (auto &part : _storiesSiblingParts) {
		part.destroy(f);
	}
}

void OverlayWidget::RendererGL::paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	if (handleHideWorkaround(f)) {
		return;
	}
	const auto factor = widget->devicePixelRatioF();
	if (_factor != factor) {
		_factor = factor;
		_ifactor = int(std::ceil(factor));
		_controlsImage.invalidate();

		// We use the fact that fade texture atlas
		// takes exactly full texture size. In case we
		// just invalidate it we may get larger image
		// in case of moving from greater _factor to lesser.
		_controlsFadeImage.destroy(&f);
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
	if (_owner->_hideWorkaround) {
		return QColor(0, 0, 0, 0);
	} else if (_owner->_fullScreenVideo) {
		return st::mediaviewVideoBg->c;
	} else {
		return st::mediaviewBg->c;
	}
}

bool OverlayWidget::RendererGL::handleHideWorkaround(QOpenGLFunctions &f) {
	// This is needed on Windows or Linux,
	// because on reopen it blinks with the last shown content.
	return _owner->_hideWorkaround != nullptr;
}

void OverlayWidget::RendererGL::paintBackground() {
	_contentBuffer->bind();
	if (const auto notch = _owner->topNotchSkip()) {
		const auto top = transformRect(QRect(0, 0, _owner->width(), notch));
		const GLfloat coords[] = {
			top.left(), top.top(),
			top.right(), top.top(),
			top.right(), top.bottom(),
			top.left(), top.bottom(),
		};
		const auto offset = kNotchOffset;
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
			QColor(0, 0, 0));
	}
}

void OverlayWidget::RendererGL::paintTransformedVideoFrame(
		ContentGeometry geometry) {
	const auto data = _owner->videoFrameWithInfo();
	if (data.format == Streaming::FrameFormat::None) {
		return;
	} else if (data.format == Streaming::FrameFormat::ARGB32) {
		Assert(!data.image.isNull());
		paintTransformedStaticContent(
			data.image,
			geometry,
			data.alpha,
			data.alpha);
		return;
	}
	Assert(!data.yuv->size.isEmpty());
	const auto program = (data.format == Streaming::FrameFormat::NV12)
		? &*_nv12Program
		: &*_yuv420Program;
	program->bind();
	const auto nv12 = (data.format == Streaming::FrameFormat::NV12);
	const auto yuv = data.yuv;
	const auto nv12changed = (_chromaNV12 != nv12);

	const auto upload = (_trackFrameIndex != data.index)
		|| (_streamedIndex != _owner->streamedIndex());
	_trackFrameIndex = data.index;
	_streamedIndex = _owner->streamedIndex();

	_f->glActiveTexture(GL_TEXTURE0);
	_textures.bind(*_f, 3);
	if (upload) {
		_f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		uploadTexture(
			GL_ALPHA,
			GL_ALPHA,
			yuv->size,
			_lumaSize,
			yuv->y.stride,
			yuv->y.data);
		_lumaSize = yuv->size;
	}
	_f->glActiveTexture(GL_TEXTURE1);
	_textures.bind(*_f, 4);
	if (upload) {
		uploadTexture(
			nv12 ? GL_RG : GL_ALPHA,
			nv12 ? GL_RG : GL_ALPHA,
			yuv->chromaSize,
			nv12changed ? QSize() : _chromaSize,
			yuv->u.stride / (nv12 ? 2 : 1),
			yuv->u.data);
		if (nv12) {
			_chromaSize = yuv->chromaSize;
			_f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		}
		_chromaNV12 = nv12;
	}

	validateControlsFade();
	if (nv12) {
		_f->glActiveTexture(GL_TEXTURE2);
		_controlsFadeImage.bind(*_f);
	} else {
		_f->glActiveTexture(GL_TEXTURE2);
		_textures.bind(*_f, 5);
		if (upload) {
			uploadTexture(
				GL_ALPHA,
				GL_ALPHA,
				yuv->chromaSize,
				_chromaSize,
				yuv->v.stride,
				yuv->v.data);
			_chromaSize = yuv->chromaSize;
			_f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		}

		_f->glActiveTexture(GL_TEXTURE3);
		_controlsFadeImage.bind(*_f);
	}
	program->setUniformValue("y_texture", GLint(0));
	if (nv12) {
		program->setUniformValue("uv_texture", GLint(1));
	} else {
		program->setUniformValue("u_texture", GLint(1));
		program->setUniformValue("v_texture", GLint(2));
	}
	program->setUniformValue("f_texture", GLint(nv12 ? 2 : 3));

	toggleBlending(geometry.roundRadius > 0.);
	paintTransformedContent(program, geometry, false);
}

void OverlayWidget::RendererGL::paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry,
		bool semiTransparent,
		bool fillTransparentBackground,
		int index) {
	Expects(index >= 0 && index < 3);
	Expects(image.isNull()
		|| image.format() == QImage::Format_RGB32
		|| image.format() == QImage::Format_ARGB32_Premultiplied);

	if (geometry.rect.isEmpty()) {
		return;
	}

	auto &program = fillTransparentBackground
		? _withTransparencyProgram
		: _staticContentProgram;
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
	_textures.bind(*_f, index);
	const auto cacheKey = image.isNull() ? qint64(-1) : image.cacheKey();
	const auto upload = (_cacheKeys[index] != cacheKey);
	if (upload) {
		_cacheKeys[index] = cacheKey;
		if (image.isNull()) {
			// Upload transparent 2x2 texture.
			const auto stride = 2;
			const uint32_t data[4] = { 0 };
			uploadTexture(
				Ui::GL::kFormatRGBA,
				Ui::GL::kFormatRGBA,
				QSize(2, 2),
				_rgbaSize[index],
				stride,
				data);
			_rgbaSize[index] = QSize(2, 2);
		} else {
			const auto stride = image.bytesPerLine() / 4;
			const auto data = image.constBits();
			uploadTexture(
				Ui::GL::kFormatRGBA,
				Ui::GL::kFormatRGBA,
				image.size(),
				_rgbaSize[index],
				stride,
				data);
			_rgbaSize[index] = image.size();
		}
	}

	validateControlsFade();
	_f->glActiveTexture(GL_TEXTURE1);
	_controlsFadeImage.bind(*_f);

	program->setUniformValue("s_texture", GLint(0));
	program->setUniformValue("f_texture", GLint(1));

	toggleBlending((geometry.roundRadius > 0.)
		|| (semiTransparent && !fillTransparentBackground));
	paintTransformedContent(&*program, geometry, fillTransparentBackground);
}

void OverlayWidget::RendererGL::paintTransformedContent(
		not_null<QOpenGLShaderProgram*> program,
		ContentGeometry geometry,
		bool fillTransparentBackground) {
	const auto rect = scaleRect(
		transformRect(geometry.rect),
		geometry.scale);
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

	_contentBuffer->bind();
	_contentBuffer->write(0, coords, sizeof(coords));

	program->setUniformValue("viewport", _uniformViewport);
	if (_owner->_stories) {
		const auto &top = st::storiesShadowTop.size();
		const auto shadowTop = geometry.topShadowShown
			? geometry.rect.y()
			: geometry.rect.y() - top.height();
		program->setUniformValue(
			"shadowTopRect",
			Uniform(transformRect(
				QRect(QPoint(geometry.rect.x(), shadowTop), top))));
	} else {
		const auto &top = st::mediaviewShadowTop.size();
		const auto point = QPoint(
			_shadowTopFlip ? 0 : (_viewport.width() - top.width()),
			0);
		program->setUniformValue(
			"shadowTopRect",
			Uniform(transformRect(QRect(point, top))));
	}
	const auto &bottom = _owner->_stories
		? st::storiesShadowBottom
		: st::mediaviewShadowBottom;
	program->setUniformValue("shadowBottomSkipOpacityFullFade", QVector4D(
		bottom.height() * _factor,
		geometry.bottomShadowSkip * _factor,
		geometry.controlsOpacity,
		1.f - float(geometry.fade)));
	if (!fillTransparentBackground) {
		program->setUniformValue(
			"roundRect",
			geometry.roundRadius ? Uniform(rect) : QVector4D(
				0,
				0,
				_uniformViewport.x(),
				_uniformViewport.y()));
		program->setUniformValue(
			"roundRadius",
			GLfloat(geometry.roundRadius * _factor));
	}
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
		p.translate(-outer.topLeft());
		_owner->paintThemePreviewContent(p, outer, outer);
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
		Over control,
		QRect over,
		float64 overOpacity,
		QRect inner,
		float64 innerOpacity,
		const style::icon &icon) {
	const auto meta = controlMeta(control);
	Assert(meta.icon == &icon);

	const auto overAlpha = overOpacity * kOverBackgroundOpacity;
	const auto offset = kControlsOffset + (meta.index * kControlValues) / 4;
	const auto fgOffset = offset + 4;
	const auto overRect = _controlsImage.texturedRect(
		over,
		_controlsTextures[kControlsCount]);
	const auto overGeometry = transformRect(over);
	const auto iconRect = _controlsImage.texturedRect(
		inner,
		_controlsTextures[meta.index]);
	const auto iconGeometry = transformRect(iconRect.geometry);
	const GLfloat coords[] = {
		overGeometry.left(), overGeometry.top(),
		overRect.texture.left(), overRect.texture.bottom(),

		overGeometry.right(), overGeometry.top(),
		overRect.texture.right(), overRect.texture.bottom(),

		overGeometry.right(), overGeometry.bottom(),
		overRect.texture.right(), overRect.texture.top(),

		overGeometry.left(), overGeometry.bottom(),
		overRect.texture.left(), overRect.texture.top(),

		iconGeometry.left(), iconGeometry.top(),
		iconRect.texture.left(), iconRect.texture.bottom(),

		iconGeometry.right(), iconGeometry.top(),
		iconRect.texture.right(), iconRect.texture.bottom(),

		iconGeometry.right(), iconGeometry.bottom(),
		iconRect.texture.right(), iconRect.texture.top(),

		iconGeometry.left(), iconGeometry.bottom(),
		iconRect.texture.left(), iconRect.texture.top(),
	};
	_controlsProgram->bind();
	_controlsProgram->setUniformValue("viewport", _uniformViewport);
	_contentBuffer->bind();
	if (!over.isEmpty() && overOpacity > 0) {
		_contentBuffer->write(
			offset * 4 * sizeof(GLfloat),
			coords,
			sizeof(coords));
		_controlsProgram->setUniformValue("g_opacity", GLfloat(overAlpha));
		FillTexturedRectangle(*_f, &*_controlsProgram, offset);
	} else {
		_contentBuffer->write(
			fgOffset * 4 * sizeof(GLfloat),
			coords + (fgOffset - offset) * 4,
			sizeof(coords) - (fgOffset - offset) * 4 * sizeof(GLfloat));
	}
	_controlsProgram->setUniformValue("g_opacity", GLfloat(innerOpacity));
	FillTexturedRectangle(*_f, &*_controlsProgram, fgOffset);
}

auto OverlayWidget::RendererGL::controlMeta(Over control) const -> Control {
	const auto stories = [&] {
		return (_owner->_stories != nullptr);
	};
	switch (control) {
	case Over::Left: return {
		0,
		stories() ? &st::storiesLeft : &st::mediaviewLeft
	};
	case Over::Right: return {
		1,
		stories() ? &st::storiesRight : &st::mediaviewRight
	};
	case Over::Save: return {
		2,
		(_owner->saveControlLocked()
			? &st::mediaviewSaveLocked
			: &st::mediaviewSave)
	};
	case Over::Share: return { 3, &st::mediaviewShare };
	case Over::Rotate: return { 4, &st::mediaviewRotate };
	case Over::More: return { 5, &st::mediaviewMore };
	}
	Unexpected("Control value in OverlayWidget::RendererGL::ControlIndex.");
}

void OverlayWidget::RendererGL::validateControls() {
	if (!_controlsImage.image().isNull()) {
		return;
	}
	const auto metas = {
		controlMeta(Over::Left),
		controlMeta(Over::Right),
		controlMeta(Over::Save),
		controlMeta(Over::Share),
		controlMeta(Over::Rotate),
		controlMeta(Over::More),
	};
	auto maxWidth = 0;
	auto fullHeight = 0;
	for (const auto &meta : metas) {
		maxWidth = std::max(meta.icon->width(), maxWidth);
		fullHeight += meta.icon->height();
	}
	maxWidth = std::max(st::mediaviewIconOver, maxWidth);
	fullHeight += st::mediaviewIconOver;
	auto image = QImage(
		QSize(maxWidth, fullHeight) * _ifactor,
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(_ifactor);
	{
		auto p = QPainter(&image);
		auto index = 0;
		auto height = 0;
		for (const auto &meta : metas) {
			meta.icon->paint(p, 0, height, maxWidth);
			_controlsTextures[index++] = QRect(
				QPoint(0, height) * _ifactor,
				meta.icon->size() * _ifactor);
			height += meta.icon->height();
		}
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(OverBackgroundColor());
		p.drawEllipse(
			QRect(0, height, st::mediaviewIconOver, st::mediaviewIconOver));
		_controlsTextures[index++] = QRect(
			QPoint(0, height) * _ifactor,
			QSize(st::mediaviewIconOver, st::mediaviewIconOver) * _ifactor);
		height += st::mediaviewIconOver;
	}
	_controlsImage.setImage(std::move(image));
}

void OverlayWidget::RendererGL::invalidateControls() {
	_controlsImage.invalidate();
	ranges::fill(_controlsTextures, QRect());
}

void OverlayWidget::RendererGL::validateControlsFade() {
	const auto forStories = (_owner->_stories != nullptr);
	const auto flip = !forStories && !_owner->topShadowOnTheRight();
	if (!_controlsFadeImage.image().isNull()
		&& _shadowTopFlip == flip
		&& _shadowsForStories == forStories) {
		return;
	}
	_shadowTopFlip = flip;
	_shadowsForStories = forStories;
	const auto &top = _shadowsForStories
		? st::storiesShadowTop
		: st::mediaviewShadowTop;
	const auto &bottom = _shadowsForStories
		? st::storiesShadowBottom
		: st::mediaviewShadowBottom;
	const auto width = top.width();
	const auto bottomTop = top.height();
	const auto height = bottomTop + bottom.height();

	auto image = QImage(
		QSize(width, height) * _ifactor,
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(_ifactor);

	auto p = QPainter(&image);
	top.paint(p, 0, 0, width);
	bottom.fill(
		p,
		QRect(0, bottomTop, width, bottom.height()));
	p.end();

	if (flip) {
		image = std::move(image).mirrored(true, false);
	}

	_controlsFadeImage.setImage(std::move(image));
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

void OverlayWidget::RendererGL::paintRoundedCorners(int radius) {
	const auto topLeft = transformRect(QRect(0, 0, radius, radius));
	const auto topRight = transformRect(
		QRect(_viewport.width() - radius, 0, radius, radius));
	const auto bottomRight = transformRect(QRect(
		_viewport.width() - radius,
		_viewport.height() - radius,
		radius,
		radius));
	const auto bottomLeft = transformRect(
		QRect(0, _viewport.height() - radius, radius, radius));
	const GLfloat coords[] = {
		topLeft.left(), topLeft.top(),
		topLeft.right(), topLeft.top(),
		topLeft.right(), topLeft.bottom(),
		topLeft.right(), topLeft.bottom(),
		topLeft.left(), topLeft.bottom(),
		topLeft.left(), topLeft.top(),

		topRight.left(), topRight.top(),
		topRight.right(), topRight.top(),
		topRight.right(), topRight.bottom(),
		topRight.right(), topRight.bottom(),
		topRight.left(), topRight.bottom(),
		topRight.left(), topRight.top(),

		bottomRight.left(), bottomRight.top(),
		bottomRight.right(), bottomRight.top(),
		bottomRight.right(), bottomRight.bottom(),
		bottomRight.right(), bottomRight.bottom(),
		bottomRight.left(), bottomRight.bottom(),
		bottomRight.left(), bottomRight.top(),

		bottomLeft.left(), bottomLeft.top(),
		bottomLeft.right(), bottomLeft.top(),
		bottomLeft.right(), bottomLeft.bottom(),
		bottomLeft.right(), bottomLeft.bottom(),
		bottomLeft.left(), bottomLeft.bottom(),
		bottomLeft.left(), bottomLeft.top(),
	};
	const auto offset = kControlsOffset
		+ (kControlsCount * kControlValues) / 4;
	const auto byteOffset = offset * 4 * sizeof(GLfloat);
	_contentBuffer->bind();
	_contentBuffer->write(byteOffset, coords, sizeof(coords));
	_roundedCornersProgram->bind();
	_roundedCornersProgram->setUniformValue("viewport", _uniformViewport);
	const auto roundRect = transformRect(QRect(QPoint(), _viewport));
	_roundedCornersProgram->setUniformValue("roundRect", Uniform(roundRect));
	_roundedCornersProgram->setUniformValue(
		"roundRadius",
		GLfloat(radius * _factor));

	_f->glEnable(GL_BLEND);
	_f->glBlendFunc(GL_ZERO, GL_SRC_ALPHA);

	GLint position = _roundedCornersProgram->attributeLocation("position");
	_f->glVertexAttribPointer(
		position,
		2,
		GL_FLOAT,
		GL_FALSE,
		2 * sizeof(GLfloat),
		reinterpret_cast<const void*>(byteOffset));
	_f->glEnableVertexAttribArray(position);

	_f->glDrawArrays(GL_TRIANGLES, 0, base::array_size(coords) / 2);

	_f->glDisableVertexAttribArray(position);
}

void OverlayWidget::RendererGL::paintStoriesSiblingPart(
		int index,
		const QImage &image,
		QRect rect,
		float64 opacity) {
	Expects(index >= 0 && index < kStoriesSiblingPartsCount);

	if (image.isNull() || rect.isEmpty()) {
		return;
	}

	_f->glActiveTexture(GL_TEXTURE0);

	auto &part = _storiesSiblingParts[index];
	part.setImage(image);
	part.bind(*_f);

	const auto textured = part.texturedRect(
		rect,
		QRect(QPoint(), image.size()));
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
	const auto offset = kControlsOffset
		+ (kControlsCount * kControlValues) / 4
		+ (6 * 2 * 4) / 4 // rounding
		+ (index * 4);
	const auto byteOffset = offset * 4 * sizeof(GLfloat);
	_contentBuffer->bind();
	_contentBuffer->write(byteOffset, coords, sizeof(coords));

	_controlsProgram->bind();
	_controlsProgram->setUniformValue("viewport", _uniformViewport);
	_contentBuffer->write(
		offset * 4 * sizeof(GLfloat),
		coords,
		sizeof(coords));
	_controlsProgram->setUniformValue("g_opacity", GLfloat(opacity));
	FillTexturedRectangle(*_f, &*_controlsProgram, offset);
}

//
//void OverlayWidget::RendererGL::invalidate() {
//	_trackFrameIndex = -1;
//	_streamedIndex = -1;
//	const auto images = {
//		&_radialImage,
//		&_documentBubbleImage,
//		&_themePreviewImage,
//		&_saveMsgImage,
//		&_footerImage,
//		&_captionImage,
//		&_groupThumbsImage,
//		&_controlsImage,
//	};
//	for (const auto image : images) {
//		image->setImage(QImage());
//	}
//	invalidateControls();
//}

void OverlayWidget::RendererGL::paintUsingRaster(
		Ui::GL::Image &image,
		QRect rect,
		Fn<void(Painter&&)> method,
		int bufferOffset,
		bool transparent) {
	auto raster = image.takeImage();
	const auto size = rect.size() * _ifactor;
	if (raster.width() < size.width() || raster.height() < size.height()) {
		raster = QImage(size, QImage::Format_ARGB32_Premultiplied);
		Assert(!raster.isNull());
		raster.setDevicePixelRatio(_ifactor);
		if (!transparent
			&& (raster.width() > size.width()
				|| raster.height() > size.height())) {
			raster.fill(Qt::transparent);
		}
	} else if (raster.devicePixelRatio() != _ifactor) {
		raster.setDevicePixelRatio(_ifactor);
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
	_contentBuffer->bind();
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

Rect OverlayWidget::RendererGL::scaleRect(
		const Rect &unscaled,
		float64 scale) const {
	const auto added = scale - 1.;
	const auto addw = unscaled.width() * added;
	const auto addh = unscaled.height() * added;
	return Rect(
		unscaled.x() - addw / 2,
		unscaled.y() - addh / 2,
		unscaled.width() + addw,
		unscaled.height() + addh);
}

} // namespace Media::View
